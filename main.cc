#include "parser.h"
#include "handler.h"
#include "options.h"
#include <tclap/CmdLine.h>

#include <rapidjson/document.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <stack>
#include <deque>
#include <unordered_set>

std::fstream* outputStreamPtr = nullptr;

//----------------------------------------------------------------------------------------------------
void print_usage()
{
  std::cout << "Usage: inputFile" << std::endl;
}
//----------------------------------------------------------------------------------------------------

constexpr auto bodyGenerationPrefab = "#include \"../Misc.h\"\n"
"#include <array>\n"
"#include <algorithm>\n"
"#include <boost/serialization/export.hpp>\n"
"#ifdef GENERATE_BODY\n"
"#undef GENERATE_BODY\n"
"#endif\n"
"#define GENERATE_BODY typedef {1} Base;"
"static std::string_view StaticTypeName()"
"{{"
"return static_type_name<{0}>::name();"
"}}"
"static std::string_view StaticFullTypeName()"
"{{"
"return static_type_name<{0}>::full_name();"
"}}"
"static HashType StaticTypeHash()"
"{{"
"return type_hash<{0}>::value;"
"}}"
"static bool StaticIsBaseOf(HashType hash)"
"{{"
"return polymorphic_type_hash<{0}>::is_base_of(hash);"
"}}"
"virtual std::string_view GetTypeName() const {{ return {0}::StaticFullTypeName(); }}"
"virtual std::string_view GetPrettyTypeName() const {{ return {0}::StaticTypeName(); }}"
"virtual HashType GetTypeHash() const {{ return {0}::StaticTypeHash(); }}"
"virtual bool IsBaseOf(HashType hash) const {{ return {0}::StaticIsBaseOf(hash); }} ";

constexpr auto registerBoostType = "namespace {0} {{{1} {2};}}\n"
"BOOST_CLASS_EXPORT_KEY({0}::{2})\n";

constexpr auto registerBoostTypeAbstract = "namespace {0} {{{1} {2};}}\n"
"BOOST_SERIALIZATION_ASSUME_ABSTRACT({0}::{2})\n";

constexpr auto staticTypePrefab = "template <> struct polymorphic_type_hash<{0}>" 
"{{"
"static constexpr size_t upcast_count = 1 + polymorphic_type_hash<{1}>::upcast_count;"
"static constexpr std::array<HashType, upcast_count> upcast_array = []"
"{{"
"std::array<HashType, upcast_count> ret{{ type_hash<{0}>::value }};"
"std::copy_n(polymorphic_type_hash<{1}>::upcast_array.begin(), polymorphic_type_hash<{1}>::upcast_array.size(), ret.data() + 1);"
"return ret;" 
"}}();"
"static bool is_base_of(const HashType hash)" 
"{{"
"if constexpr ((upcast_count * sizeof(HashType)) < (1 << 7))"
"{{"
"return std::ranges::find(upcast_array, hash) != upcast_array.end();"
"}}"
"static bool first_run = true; static std::array<HashType, upcast_count> sorted_upcast = upcast_array;"
"if (first_run)"
"{{"
"std::sort(sorted_upcast.begin(), sorted_upcast.end(), std::less<int const*>());"
"first_run = false;" 
"}}"
"return std::ranges::binary_search(sorted_upcast, hash);" 
"}}"
"}}; ";

constexpr auto serializeInlineDeclStart =
"friend class Engine::Serializer; friend class boost::serialization::access; private: template <class Archive> void serialize(Archive &ar, const unsigned int file_version) {";
constexpr auto serializeBaseClassAr = "ar& boost::serialization::base_object<{0}>(*this); ";
constexpr auto serializePropertyAr = "ar& {0}; ";
constexpr auto serializeInlineDeclEnd = "} public: ";

//----------------------------------------------------------------------------------------------------
std::string GenerateSerializationDeclaration(const rapidjson::Value* val, bool isNativeBaseClass, const std::string& baseClass)
{
    std::string serializeBody = serializeInlineDeclStart;
    if (isNativeBaseClass) 
    {
        serializeBody += std::format(serializeBaseClassAr, baseClass);
    }
    
    std::vector<std::string> propertyNames;

    if ((*val)["members"].Empty())
    {
        serializeBody += serializeInlineDeclEnd;
        return serializeBody + "\n";
    }

    for (auto it = (*val)["members"].Begin(); it != (*val)["members"].End(); ++it)
    {
        if ((*it)["type"] == "property")
        {
            std::cout << "Found property " << (*it)["name"].GetString() << std::endl;
            propertyNames.push_back((*it)["name"].GetString());
        }
    }

    if (!propertyNames.empty())
    {
        for (const std::string& property : propertyNames)
        {
            serializeBody += std::format(serializePropertyAr, property);
        }
    }

    serializeBody += serializeInlineDeclEnd;

    return serializeBody + "\n";
}

void ReconstructBaseClassNamespace(const std::string& joinedNamespace, std::string& outBaseClass, std::string& outBaseClassNamespace)
{
    if (outBaseClass.find("::") == std::string::npos)
    {
        // same namespace, namespaces were discarded.
        outBaseClassNamespace = joinedNamespace;
    }
    else
    {
        const std::string baseClassScope = outBaseClassNamespace.substr(0, outBaseClass.find_first_of("::"));
        const std::string classScope = joinedNamespace.substr(0, joinedNamespace.find_first_of("::"));

        if (baseClassScope == classScope)
        {
            // root namescope is same but other than that, every namespaces are different.
            __nop();
        }
        else
        {
            // Drop the first different namespace and concat with the base class namespace
            const size_t parentScope = joinedNamespace.substr(0, joinedNamespace.size() - 2).rfind("::");

            outBaseClassNamespace = joinedNamespace.substr(0, parentScope) + outBaseClassNamespace;
        }
    }
}

//----------------------------------------------------------------------------------------------------
void TestTags(const std::string& joinedNamespace, const rapidjson::Value* it) 
{
    assert(outputStreamPtr != nullptr);

    if ((*it)["type"] == "class")
    {
        const std::string className = (*it)["name"].GetString();
        std::string baseClass;
        std::string baseClassNamespace;
        bool isNativeBaseClass = true;

        if ((*it)["parents"].Empty()) 
        {
            baseClass = "void";
            isNativeBaseClass = false;
        }
        else if (const std::string baseTypeName = (*it)["parents"][0]["name"]["name"].GetString(); 
            baseTypeName.substr(0, 5).find("boost") != std::string::npos)
        {
            // not a native class.
            baseClass = "void";
            isNativeBaseClass = false;
        }
        else 
        {
            baseClass = (*it)["parents"][0]["name"]["name"].GetString();

            // Check if any namespace persists.
            if (const size_t namespaceOffset = baseClass.rfind("::");
                namespaceOffset != std::string::npos) 
            {
                baseClassNamespace = baseClass.substr(0, namespaceOffset);
                baseClass = baseClass.substr(namespaceOffset + 2, baseClass.size() - (namespaceOffset + 2));
            }
        }

        if (isNativeBaseClass) 
        {
            ReconstructBaseClassNamespace(joinedNamespace, baseClass, baseClassNamespace);
        }
        
        std::cout << "Class parsed with " << joinedNamespace + className << " and " << baseClassNamespace + baseClass << std::endl;

        *outputStreamPtr << std::format(bodyGenerationPrefab, joinedNamespace + className, baseClassNamespace + baseClass) + GenerateSerializationDeclaration(it, isNativeBaseClass, baseClass);

        if (((*it)["meta"]).HasMember("abstract")) 
        {
            *outputStreamPtr << std::format(registerBoostTypeAbstract, joinedNamespace.substr(0, joinedNamespace.size() - 2), "class", className);
        }
        else 
        {
            *outputStreamPtr << std::format(registerBoostType, joinedNamespace.substr(0, joinedNamespace.size() - 2), "class", className);
        }

        *outputStreamPtr << std::format(staticTypePrefab, joinedNamespace + className, baseClassNamespace + baseClass);
    }
}

void RecurseNamespace(const rapidjson::Value* root, std::deque<std::string> currentNamespace)
{
    const rapidjson::Value& ref = *root;

    if (ref["type"] == "namespace")
    {
        currentNamespace.push_back(ref["name"].GetString());

        if (!ref["members"].Empty())
        {
            for (auto it = ref["members"].End() - 1; it != ref["members"].Begin() - 1; --it)
            {
                if ((*it)["type"] == "namespace")
                {
                    std::deque<std::string> nextNameSpace = currentNamespace;
                    RecurseNamespace(it, nextNameSpace);
                    continue;
                }

                std::string joinedNamespace;
                for (const std::string& identifier : currentNamespace)
                {
                    joinedNamespace += identifier + "::";
                }

                std::cout << "Test header tags with namespace " << joinedNamespace << std::endl;
                TestTags(joinedNamespace, it);
            }
        }
    }
}

//----------------------------------------------------------------------------------------------------
int main(int argc, char** argv)
{
  Options options;
  std::string inputFile;
  try
  {
    using namespace TCLAP;

    CmdLine cmd("Header Parser");

    ValueArg<std::string> enumName("e", "enum", "The name of the enum macro", false, "ENUM", "", cmd);
    ValueArg<std::string> className("c", "class", "The name of the class macro", false, "CLASS", "", cmd);
    ValueArg<std::string> constructorName("q", "constructor", "The name of the constructor macro", false, "CONSTRUCTOR", "", cmd);
    MultiArg<std::string> functionName("f", "function", "The name of the function macro", false, "", cmd);
    ValueArg<std::string> propertyName("p", "property", "The name of the property macro", false, "PROPERTY", "", cmd);
    MultiArg<std::string> customMacro("m", "macro", "Custom macro names to parse", false, "", cmd);
    UnlabeledValueArg<std::string> inputFileArg("inputFile", "The file to process", true, "", "", cmd);

    cmd.parse(argc, argv);

    inputFile = inputFileArg.getValue();
    options.classNameMacro = className.getValue();
    options.enumNameMacro = enumName.getValue();
    options.functionNameMacro = functionName.getValue();
    options.customMacros = customMacro.getValue();
    options.propertyNameMacro = propertyName.getValue();
    options.constructorNameMacro = constructorName.getValue();
  }
  catch (TCLAP::ArgException& e)
  {
    std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
    return -1;
  }

  // Open from file
  std::ifstream t(inputFile);
  if (!t.is_open())
  {
    std::cerr << "Could not open " << inputFile << std::endl;
    return -1;
  }

  std::stringstream buffer;
  buffer << t.rdbuf();

  Parser parser(options);
  if (parser.Parse(buffer.str().c_str()))
  {
      std::filesystem::path path = "HeaderGenerated/" + inputFile;
      path.replace_extension(".generated.h");

      if (!std::filesystem::exists(path.parent_path())) 
      {
          std::cout << "Create directory " << path.parent_path() << std::endl;
          std::filesystem::create_directories(path.parent_path());
      }

      std::fstream outputSteam(path.c_str(), std::ios::out);

      if (!outputSteam.is_open()) 
      {
          std::cerr << "Unable to create a file " << path << std::endl;
          return -1;
      }

      outputStreamPtr = &outputSteam;

      rapidjson::Document document;
      document.Parse(parser.result().c_str());
      assert(document.IsArray());

      for (auto it = document.End() - 1; it != document.Begin() - 1; --it) 
      {
          RecurseNamespace(it, {});
      }

      outputSteam.close();
  }
  
	return 0;
}
