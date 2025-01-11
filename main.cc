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
#include <sstream>

std::fstream* outputStreamPtr = nullptr;

//----------------------------------------------------------------------------------------------------
void print_usage()
{
  std::cout << "Usage: inputFile" << std::endl;
}
//----------------------------------------------------------------------------------------------------

constexpr auto bodyGenerationStaticPrefab = "#pragma once\n"
"#include \"../Misc.h\"\n"
"#include <array>\n"
"#include <algorithm>\n"
"#include <boost/serialization/export.hpp>\n"
"#include <boost/serialization/access.hpp>\n"
"#ifdef GENERATE_BODY\n"
"#undef GENERATE_BODY\n"
"#endif\n"
"#define GENERATE_BODY public: typedef {1} Base;"
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
"}} ";

constexpr auto bodyGenerationOverridablePrefab = "#pragma once\n"
"#include \"../Misc.h\"\n"
"#include <array>\n"
"#include <algorithm>\n"
"#include <boost/serialization/export.hpp>\n"
"#include <boost/serialization/access.hpp>\n"
"#ifdef GENERATE_BODY\n"
"#undef GENERATE_BODY\n"
"#endif\n"
"#define GENERATE_BODY public: typedef {1} Base;"
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

constexpr auto bodyGenerationResourceGetterCreator =
"template <typename Void = void> requires (std::is_base_of_v<Engine::Abstracts::Resource, {0}>)\
static Engine::Weak<{0}> Get(const std::string & name) {{ return Engine::Managers::ResourceManager::GetInstance().GetResource<{0}>(name); }}\
template <typename Void = void> requires (std::is_base_of_v<Engine::Abstracts::Resource, {0}>)\
static Engine::Weak<{0}> GetByMetadataPath(const std::filesystem::path& meta_path) {{ return Engine::Managers::ResourceManager::GetInstance().GetResourceByMetadataPath<{0}>(meta_path); }} \
template <typename Void = void> requires (std::is_base_of_v<Engine::Abstracts::Resource, {0}>)\
static Engine::Weak<{0}> GetByRawPath(const std::filesystem::path& path) {{ return Engine::Managers::ResourceManager::GetInstance().GetResourceByRawPath<{0}>(path); }}\
template <typename... Args> requires (std::is_base_of_v<Engine::Abstracts::Resource, {0}>)\
static Engine::Strong<{0}> Create(const std::string_view name, Args&&... args)\
{{\
if (!name.empty() && Engine::Managers::ResourceManager::GetInstance().GetResource<{0}>(name).lock()) {{ return {{}}; }}\
const auto obj = boost::make_shared<{0}>(std::forward<Args>(args)...); \
Engine::Managers::ResourceManager::GetInstance().AddResource(name, obj); \
return obj; \
}} ";

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
"if (std::ranges::adjacent_find(ret) != std::ranges::end(ret))"
"{{"
"	throw std::exception(\"Duplicated type hash found in upcast array\");"
"}}"
"return ret;"
"}}();"
"static bool is_base_of(const HashType hash)" 
"{{"
"if constexpr ((upcast_count * sizeof(HashType)) < (1 << 7))"
"{{"
"return std::ranges::find(upcast_array, hash) != upcast_array.end();"
"}}"
"return std::ranges::binary_search(upcast_array, hash);" 
"}}"
"}}; ";

constexpr auto serializeInlineDeclStart =
"friend class Engine::Serializer; friend class boost::serialization::access; private: template <class Archive> void serialize(Archive &ar, const unsigned int file_version) {";
constexpr auto serializeBaseClassAr = "ar& boost::serialization::base_object<{0}>(*this); ";
constexpr auto serializePropertyAr = "ar& {0}; ";
constexpr auto serializeInlineDeclEnd = "} public: ";

//----------------------------------------------------------------------------------------------------
std::string GenerateSerializationDeclaration(const rapidjson::Value* val, bool isNativeBaseClass, const std::string& baseClassNamespace, const std::string& baseClass)
{
    std::string serializeBody = serializeInlineDeclStart;
    if (isNativeBaseClass) 
    {
        serializeBody += std::format(serializeBaseClassAr, baseClassNamespace + baseClass);
    }
    
    std::vector<std::string_view> propertyNames;

    if ((*val)["members"].Empty())
    {
        serializeBody += serializeInlineDeclEnd;
        return serializeBody;
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
        for (const std::string_view& property : propertyNames)
        {
            serializeBody += std::format(serializePropertyAr, property);
        }
    }

    serializeBody += serializeInlineDeclEnd;

    return serializeBody;
}

void ReconstructBaseClosureNamespaceImpl(const std::string_view joinedNamespace, std::string& outBaseClassNamespace)
{
    if (outBaseClassNamespace.find("::") == std::string::npos)
    {
        // same namespace, namespaces were discarded.
        outBaseClassNamespace = joinedNamespace;
    }
    else
    {
        const std::string baseClassScope = outBaseClassNamespace.substr(0, outBaseClassNamespace.find_first_of("::"));
        const std::string_view classScope = joinedNamespace.substr(0, joinedNamespace.find_first_of("::"));

        const size_t classSuffixOffset = joinedNamespace.substr(0, joinedNamespace.size() - 2).rfind("::");

        std::cout << std::format("base class scope: {}, class scope: {}, suffix offset: {}", baseClassScope, classScope, classSuffixOffset) << std::endl;

        if (baseClassScope == classScope)
        {
            std::cout << "Use class namespace..." << std::endl;
            // root namescope is same but other than that, every namespaces are different.
            return;
        }

        if (classSuffixOffset != std::string::npos)
        {
            if (const std::string_view classSuffixScope = joinedNamespace.substr(classSuffixOffset);
				baseClassScope == classSuffixScope)
            {
                std::cout << "Merge class namespace...";
	            // namespace can be merged.
				outBaseClassNamespace = joinedNamespace;
                return;
            }
        }
        else
        {
            std::cout << "Append to class namespace..." << std::endl;
	        outBaseClassNamespace.insert(outBaseClassNamespace.begin(), joinedNamespace.begin(), joinedNamespace.end());
            return;
        }

        const size_t parentScopeEnd = joinedNamespace.substr(0, joinedNamespace.size() - 2).rfind("::");
        const size_t parentScopeBegin = joinedNamespace.substr(0, parentScopeEnd + 2).rfind("::");

        // If namespace is one, merge with class namespace.
        if (parentScopeBegin == std::string::npos)
        {
            std::cout << "Class has one namespace, merging..." << std::endl;
            outBaseClassNamespace.insert(outBaseClassNamespace.begin(), { ':', ':'});
            outBaseClassNamespace.insert(outBaseClassNamespace.begin(), joinedNamespace.begin(), joinedNamespace.end());
            return;
        }

        std::cout << "Remove the nearest namespace and appending..." << std::endl;
        const std::string_view parentScope = joinedNamespace.substr(0, parentScopeBegin);
        outBaseClassNamespace.insert(outBaseClassNamespace.begin(), { ':', ':' });
        outBaseClassNamespace.insert(outBaseClassNamespace.begin(), parentScope.begin(), parentScope.end());
    }
}

//----------------------------------------------------------------------------------------------------

bool ReconstructBaseClosureAndNamespace(const rapidjson::Value* it, const std::string_view joinedNamespace, const std::string_view closureName, std::string& outBaseClosure, std::string& outBaseClosureNamespace) 
{
    bool isNativeBaseClass = true;

    if ((*it)["parents"].Empty())
    {
        outBaseClosure = "void";
        isNativeBaseClass = false;
    }
    else if (const std::string baseTypeName = (*it)["parents"][0]["name"]["name"].GetString();
        baseTypeName.length() >= 5 && baseTypeName.substr(0, 5).find("boost") != std::string::npos)
    {
        // not a native class.
        outBaseClosure = "void";
        isNativeBaseClass = false;
    }
    else
    {
        outBaseClosure = (*it)["parents"][0]["name"]["name"].GetString();

        if ((*it)["parents"][0]["name"]["type"] == "template")
        {
            std::stringstream argumentStream;
            const rapidjson::Value& templateArguments = (*it)["parents"][0]["name"]["arguments"];
            argumentStream << '<';
            for (auto arg = templateArguments.Begin(); arg != templateArguments.End(); ++arg)
            {
                if (std::string_view thisArg = (*arg)["name"].GetString();
                    closureName == thisArg && thisArg.rfind("::") == std::string::npos)
                {
                    std::cout << "Found class name in the argument, with no namespaces. Append the class namespace..." << std::endl;
                    argumentStream << std::format("{}{}{}", joinedNamespace, (*arg)["name"].GetString(), (arg + 1 == templateArguments.End()) ? "" : ",");
                }
                else
                {
                    argumentStream << std::format("{}{}", (*arg)["name"].GetString(), (arg + 1 == templateArguments.End()) ? "" : ",");
                }
            }
            argumentStream << '>';

            std::cout << "Found template arguments " << argumentStream.str() << std::endl;
            outBaseClosure += argumentStream.str();
        }

        // Check if any namespace persists.
        if (const size_t namespaceOffset = outBaseClosure.rfind("::");
            namespaceOffset != std::string::npos)
        {
            outBaseClosureNamespace = outBaseClosure.substr(0, namespaceOffset) + "::";
            outBaseClosure = outBaseClosure.substr(namespaceOffset + 2, outBaseClosure.size() - (namespaceOffset + 2));
        }
    }

    if (isNativeBaseClass)
    {
        ReconstructBaseClosureNamespaceImpl(joinedNamespace, outBaseClosureNamespace);
    }

    return isNativeBaseClass;
}

void TestTags(const std::string_view joinedNamespace, const rapidjson::Value* it) 
{
    assert(outputStreamPtr != nullptr);

    if ((*it)["isstruct"].GetBool() == true) 
    {
        const std::string structName = (*it)["name"].GetString();
        std::cout << "Reading struct " << structName << std::endl;
        std::string baseStruct;
        std::string baseStructNamespace;
        bool isNativeBaseClass = ReconstructBaseClosureAndNamespace(it, joinedNamespace, structName, baseStruct, baseStructNamespace);

        std::string structFullName(joinedNamespace.begin(), joinedNamespace.end());
        structFullName += structName;
        std::cout << "Struct parsed with " << structName << " and " << baseStructNamespace << baseStruct << std::endl;

    	*outputStreamPtr << std::format(bodyGenerationOverridablePrefab, structFullName, baseStructNamespace + baseStruct);

        *outputStreamPtr << GenerateSerializationDeclaration(it, isNativeBaseClass, baseStructNamespace, baseStruct);
        *outputStreamPtr << '\n';


        if (it->HasMember("meta") && ((*it)["meta"]).HasMember("abstract"))
        {
            *outputStreamPtr << std::format(registerBoostTypeAbstract, joinedNamespace.substr(0, joinedNamespace.size() - 2), "struct", structName);
        }
        else
        {
            *outputStreamPtr << std::format(registerBoostType, joinedNamespace.substr(0, joinedNamespace.size() - 2), "struct", structName);
        }

        *outputStreamPtr << std::format(staticTypePrefab, structFullName, baseStructNamespace+ baseStruct);
    }
    else
    {
        const std::string className = (*it)["name"].GetString();
        std::cout << "Reading class " << className << std::endl;
        std::string baseClass;
        std::string baseClassNamespace;
        bool isNativeBaseClass = ReconstructBaseClosureAndNamespace(it, joinedNamespace, className, baseClass, baseClassNamespace);
        
        std::string classFullName(joinedNamespace.begin(), joinedNamespace.end());
        classFullName += className;

        std::cout << "Class parsed with " << classFullName << " and " << baseClassNamespace << baseClass << std::endl;

        *outputStreamPtr << std::format(bodyGenerationOverridablePrefab, classFullName, baseClassNamespace + baseClass);

        if (it->HasMember("meta") && ((*it)["meta"]).HasMember("resource"))
        {
            *outputStreamPtr << std::format(bodyGenerationResourceGetterCreator, className);
        }

        *outputStreamPtr << GenerateSerializationDeclaration(it, isNativeBaseClass, baseClassNamespace, baseClass);
        *outputStreamPtr << '\n';

        if (it->HasMember("meta") && ((*it)["meta"]).HasMember("abstract"))
        {
            *outputStreamPtr << std::format(registerBoostTypeAbstract, joinedNamespace.substr(0, joinedNamespace.size() - 2), "class", className);
        }
        else 
        {
            *outputStreamPtr << std::format(registerBoostType, joinedNamespace.substr(0, joinedNamespace.size() - 2), "class", className);
        }

        *outputStreamPtr << std::format(staticTypePrefab, classFullName, baseClassNamespace + baseClass);
    }
}

void RecurseNamespace(const rapidjson::Value* root, std::deque<std::string_view> currentNamespace)
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
                    RecurseNamespace(it, currentNamespace);
                    continue;
                }

                std::string joinedNamespace;
                for (const std::string_view& identifier : currentNamespace)
                {
                    joinedNamespace += std::format("{}{}", identifier, "::");
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
      std::filesystem::path path = inputFile;
      path.replace_extension(".generated.h");
      path = path.parent_path().parent_path() / "HeaderGenerated" / path.parent_path().stem() / path.filename();

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

      std::cout << "== Start of " << path << " ==" << std::endl;
      std::cout << "Parsing json" << std::endl;
      rapidjson::Document document;
      document.Parse(parser.result().c_str());
      assert(document.IsArray());

      std::cout << parser.result().c_str() << std::endl;

      try 
      {
          for (auto it = document.End() - 1; it != document.Begin() - 1; --it)
          {
              RecurseNamespace(it, {});
          }
      }
      catch (std::exception e)
      {
          std::cerr << e.what() << std::endl;
          outputSteam.close();
      }

      outputSteam.close();
  }
  
	return 0;
}
