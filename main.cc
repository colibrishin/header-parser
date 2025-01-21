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
#include <vector>
#include <unordered_set>
#include <sstream>
#include <execution>

//----------------------------------------------------------------------------------------------------
void print_usage()
{
  std::cout << "Usage: inputFile" << std::endl;
}
//----------------------------------------------------------------------------------------------------

constexpr auto generatedHeaderFormat = 
"#ifdef {0}_GENERATED_H\n"
"#ifndef POST_{0}_H\n"
"#define POST_{0}_H\n"
"{3}\n"
"#endif\n"
"#endif\n"
"#ifndef {0}_GENERATED_H\n"
"#define {0}_GENERATED_H\n"
"#include \"CoreType.h\"\n"
"#include <array>\n"
"#include <algorithm>\n"
"#include <type_traits>\n"
"#include <filesystem>\n"
"#include <boost/serialization/export.hpp>\n"
"#include <boost/serialization/access.hpp>\n"
"#ifdef GENERATE_BODY\n"
"#undef GENERATE_BODY\n"
"#endif\n"
"#define GENERATE_BODY {1}\n"
"{2}\n"
"#endif\n";

constexpr auto bodyGenerationStaticPrefab =
"public: typedef {1} Base;"
"constexpr static std::string_view StaticTypeName()"
"{{"
"return static_type_name<{0}>::name();"
"}}"
"constexpr static std::string_view StaticFullTypeName()"
"{{"
"return static_type_name<{0}>::full_name();"
"}}"
"constexpr static HashType StaticTypeHash()"
"{{"
"return &type_hash<{0}>::value;"
"}}"
"constexpr static bool StaticIsDerivedOf(HashType hash)"
"{{"
"return polymorphic_type_hash<{0}>::is_derived_of(hash);"
"}} ";

constexpr auto bodyGenerationOverridablePrefab = 
"virtual std::string_view GetTypeName() const {{ return {0}::StaticFullTypeName(); }}"
"virtual std::string_view GetPrettyTypeName() const {{ return {0}::StaticTypeName(); }}"
"virtual HashType GetTypeHash() const {{ return {0}::StaticTypeHash(); }}"
"virtual bool IsDerivedOf(HashType hash) const {{ return {0}::StaticIsDerivedOf(hash); }} ";

constexpr auto bodyGenerationResourceGetterCreator =
"template <typename Void = void, typename Name> requires (std::is_base_of_v<Engine::Abstracts::Resource, {0}>, std::is_constructible_v<std::string_view, Name>)\
static Engine::Weak<{0}> Get(const Name& name) {{ return Engine::Managers::ResourceManager::GetInstance().GetResource<{0}>(name); }}\
template <typename Void = void, typename MetaPath> requires (std::is_base_of_v<Engine::Abstracts::Resource, {0}>, std::is_constructible_v<std::filesystem::path, MetaPath>)\
static Engine::Weak<{0}> GetByMetadataPath(const MetaPath& meta_path) {{ return Engine::Managers::ResourceManager::GetInstance().GetResourceByMetadataPath<{0}>(meta_path); }} \
template <typename Void = void, typename RawPath> requires (std::is_base_of_v<Engine::Abstracts::Resource, {0}>, std::is_constructible_v<std::filesystem::path, RawPath>)\
static Engine::Weak<{0}> GetByRawPath(const RawPath& path) {{ return Engine::Managers::ResourceManager::GetInstance().GetResourceByRawPath<{0}>(path); }}\
template <bool ForceLoad = false, typename Name, typename RawPath, typename... Args> requires (\
std::is_base_of_v<Engine::Abstracts::Resource, {0}>,\
std::is_constructible_v<std::string_view, Name>,\
std::is_constructible_v<std::filesystem::path, RawPath>,\
std::is_constructible_v<{0}, RawPath, Args...>)\
static Engine::Strong<{0}> Create(const Name& name, const RawPath& raw_path, Args&&... args)\
{{\
const std::string_view name_view(name);\
const std::filesystem::path path_view(raw_path);\
if (const auto& name_wise = {0}::Get(name_view).lock(); !name_view.empty() && name_wise) {{\
if (const auto& path_wise = {0}::GetByRawPath(path_view).lock(); !path_view.empty() && path_wise && name_wise == path_wise) {{return path_wise;}}\
return {{}}; }}\
const auto obj = boost::shared_ptr<{0}>(new {0}(path_view, std::forward<Args>(args)...));\
Engine::Managers::ResourceManager::GetInstance().AddResource(name_view, obj); \
if constexpr (ForceLoad) {{\
obj->Load();\
}}\
return obj; \
}}\
template <bool ForceLoad = false, typename Name, typename... Args> requires (\
std::is_base_of_v<Engine::Abstracts::Resource, {0}>,\
std::is_constructible_v<std::string_view, Name>,\
!std::is_constructible_v<{0}, std::filesystem::path, Args...>)\
static Engine::Strong<{0}> Create(const Name& name, Args&&... args)\
{{\
const std::string_view name_view(name);\
if (!name_view.empty() && Engine::Managers::ResourceManager::GetInstance().GetResource<{0}>(name_view).lock()) {{ return {{}}; }}\
const auto obj = boost::shared_ptr<{0}>(new {0}(std::forward<Args>(args)...)); \
Engine::Managers::ResourceManager::GetInstance().AddResource(name_view, obj); \
if constexpr (ForceLoad) {{\
obj->Load();\
}}\
return obj; \
}} ";

constexpr auto serializeInlineDeclStart =
"friend class Engine::Serializer; friend class boost::serialization::access; private: template <class Archive> void serialize(Archive &ar, const unsigned int file_version) {";
constexpr auto serializeBaseClassAr = "ar& boost::serialization::base_object<{0}>(*this); ";
constexpr auto serializePropertyAr = "ar& {0}; ";
constexpr auto serializeInlineDeclEnd = "} public: ";

constexpr auto staticForwardDeclaration = "namespace {0} {{{1} {2};}}\n";

constexpr auto staticTypePrefab =
"template <> struct polymorphic_type_hash<{0}>\
{{\
static constexpr size_t upcast_count = 1 + polymorphic_type_hash<{1}>::upcast_count;\
static constexpr auto upcast_array = []\
{{\
HashArray<upcast_count> ret{{&type_hash<{0}>::value}};\
std::copy_n(polymorphic_type_hash<{1}>::upcast_array.begin(),  polymorphic_type_hash<{1}>::upcast_array.size(), ret.data() + 1);\
std::ranges::sort(ret, [](const auto lhs, const auto rhs) {{return *lhs < *rhs;}});\
return ret;\
}}();\
constexpr static bool is_derived_of(const HashType hash) \
{{ \
if constexpr ((upcast_count * sizeof(HashTypeValue)) < (1 << 7)) \
{{ \
return std::ranges::find_if(upcast_array, [&hash](const auto other){{return hash->Equal(*other);}}) != upcast_array.end(); \
}} \
return std::ranges::binary_search(upcast_array, hash, [](const auto lhs, const auto rhs){{return *lhs < *rhs;}}); \
}} \
}}; ";

constexpr auto registerBoostType = 
"BOOST_CLASS_EXPORT_KEY({0}::{1})\n";

constexpr auto registerBoostTypeAbstract =
"BOOST_SERIALIZATION_ASSUME_ABSTRACT({0}::{1})\n";

constexpr auto registerBoostTypeImpl =
"BOOST_CLASS_EXPORT_IMPLEMENT({0}::{1})\n";

constexpr auto registerBoostMetaType =
"BOOST_CLASS_EXPORT_KEY(HashTypeT<{0}::{1}>)\n";

constexpr auto registerBoostMetaTypeImpl =
"BOOST_CLASS_EXPORT_IMPLEMENT(HashTypeT<{0}::{1}>)\n";

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

    if (!(*it).HasMember("parents"))
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

void TestTags(std::fstream& outputStream, const std::string_view fileName, const std::string_view joinedNamespace, const rapidjson::Value* it)
{
    if ((*it)["type"] == "class")
    {
        const std::string closureName = (*it)["name"].GetString();
        std::cout << "Reading closure " << closureName << std::endl;
        std::string baseClosure;
        std::string baseClosureNamespace;
        bool        isNativeBaseClass = ReconstructBaseClosureAndNamespace
            (it, joinedNamespace, closureName, baseClosure, baseClosureNamespace);

        std::string closureFullName(joinedNamespace.begin(), joinedNamespace.end());
        closureFullName += closureName;
        std::cout << "Closure parsed with " << closureName << " and " << baseClosureNamespace << baseClosure << std::endl;

        std::stringstream bodyGenerated;
        std::stringstream staticsGenerated;
        std::stringstream postGenerated;

    	bodyGenerated << std::format(bodyGenerationStaticPrefab, closureFullName, baseClosureNamespace + baseClosure);

        std::string closureType;
        if ((*it)["isstruct"].GetBool() == true)
        {
            closureType = "struct";

            if (it->HasMember("meta") && (*it)["meta"].HasMember("virtual"))
            {
                bodyGenerated << std::format(bodyGenerationOverridablePrefab, closureName);
            }
        }
        else
        {
            closureType = "class";

            if (it->HasMember("meta") && ((*it)["meta"]).HasMember("resource"))
            {
                bodyGenerated << std::format(bodyGenerationResourceGetterCreator, closureName);
            }

            bodyGenerated << std::format(bodyGenerationOverridablePrefab, closureName);
        }

        bodyGenerated << GenerateSerializationDeclaration(it, isNativeBaseClass, baseClosureNamespace, baseClosure);
        
        staticsGenerated << std::format(staticForwardDeclaration, joinedNamespace.substr(0, joinedNamespace.size() - 2), closureType, closureName);

        if (it->HasMember("meta"))
        {
            if ((*it)["meta"].HasMember("serialize"))
            {
                if (it->HasMember("meta") && (*it)["meta"].HasMember("abstract"))
                {
                    staticsGenerated << std::format
                        (
                         registerBoostTypeAbstract, joinedNamespace.substr
                         (0, joinedNamespace.size() - 2), closureName
                        );
                }
                else
                {
                    staticsGenerated << std::format(registerBoostType, joinedNamespace.substr(0, joinedNamespace.size() - 2), closureName);
                }

                postGenerated << std::format(registerBoostTypeImpl, joinedNamespace.substr(0, joinedNamespace.size() - 2), closureName);

                staticsGenerated << std::format(registerBoostMetaType, joinedNamespace.substr(0, joinedNamespace.size() - 2), closureName);
                postGenerated << std::format(registerBoostMetaTypeImpl, joinedNamespace.substr(0, joinedNamespace.size() - 2), closureName);
            }
        }
        
        staticsGenerated << std::format(staticTypePrefab, closureFullName, baseClosureNamespace + baseClosure);

        std::string nameUpperString = closureName;
        std::ranges::transform(nameUpperString, nameUpperString.begin(), [](const char& c){return std::toupper(c);});
        
        outputStream << std::format(generatedHeaderFormat, nameUpperString, bodyGenerated.str(), staticsGenerated.str(), postGenerated.str());
    }
}

void RecurseNamespace(std::fstream& outputStream, const std::string_view fileName, const rapidjson::Value* root, std::vector<std::string_view>& currentNamespace)
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
                    RecurseNamespace(outputStream, fileName, it, currentNamespace);
                    continue;
                }

                std::string joinedNamespace;
                for (const std::string_view& identifier : currentNamespace)
                {
                    joinedNamespace += std::format("{}{}", identifier, "::");
                }

                std::cout << "Test header tags with namespace " << joinedNamespace << std::endl;
                TestTags(outputStream, fileName, joinedNamespace, it);
            }
        }
    }
}

//----------------------------------------------------------------------------------------------------
int main(int argc, char** argv)
{
  Options options;
  std::vector<std::string> inputFiles;
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
    UnlabeledMultiArg<std::string> inputFileArg("inputFile", "The file to process", true, "", cmd);

    cmd.parse(argc, argv);

    inputFiles = inputFileArg.getValue();
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
  std::for_each
		  (
		   std::execution::par_unseq, inputFiles.begin(), inputFiles.end(), [&options](const std::string& inputFile)
		   {
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
				   path = path.parent_path().parent_path() / "HeaderGenerated" / path.parent_path().stem() / path.
				          filename();

				   if (!exists(path.parent_path()))
				   {
					   std::cout << "Create directory " << path.parent_path() << std::endl;
					   create_directories(path.parent_path());
				   }

				   std::fstream outputSteam(path.c_str(), std::ios::out);

				   if (!outputSteam.is_open())
				   {
					   std::cerr << "Unable to create a file " << path << std::endl;
					   return -1;
                   }

				   std::cout << "== Start of " << path << " ==" << std::endl;
				   std::cout << "Parsing json" << std::endl;
				   rapidjson::Document document;
				   document.Parse(parser.result().c_str());
				   assert(document.IsArray());

				   std::cout << parser.result().c_str() << std::endl;

				   try
				   {
					   std::string                   filename = path.filename().generic_string();
					   std::vector<std::string_view> queue{};

					   for (auto it = document.End() - 1; it != document.Begin() - 1; --it)
					   {
						   queue.reserve(64);
						   RecurseNamespace(outputSteam, filename, it, queue);
						   queue.clear();
					   }
				   }
				   catch (std::exception& e)
				   {
					   std::cerr << e.what() << std::endl;
					   outputSteam.close();
				   }

				   outputSteam.close();
			   }

            return 0;
		   }
		  );
  
  
	return 0;
}
