 #include "parser.h"
#include "handler.h"
#include "options.h"
#include <tclap/CmdLine.h>
#include <regex>

#include <rapidjson/document.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <stack>
#include <vector>
#include <unordered_set>
#include <execution>
#include <functional>
#include <set>
#include <cwctype>

#include "bodygeneration_macro.h"
#include "postpone_macro.h"
#include "dependency_template.h"

//----------------------------------------------------------------------------------------------------

struct GreaterComparer
{
    bool operator()(const std::pair<size_t, std::function<void()>>& lhs, const std::pair<size_t, std::function<void()>>& rhs)
    {
        return std::greater<size_t>{}(std::get<0>(lhs), std::get<0>(rhs));
    }
};

//----------------------------------------------------------------------------------------------------

static std::string DropFirstDirectory(const std::filesystem::path& path) 
{
    std::string srcPathStr = path.generic_string();
    std::transform(srcPathStr.begin(), srcPathStr.end(), srcPathStr.begin(), [](const char c)
        {
            if (c == '\\')
            {
                return '/';
            }

            return c;
        });

    const size_t& firstSeperator = srcPathStr.find_first_of(L'/');
    return { srcPathStr.begin() + firstSeperator + 1, srcPathStr.end() };
}

static std::string GetFirstDirectory(const std::filesystem::path& path)
{
    std::string srcPathStr = path.generic_string();
    std::transform(srcPathStr.begin(), srcPathStr.end(), srcPathStr.begin(), [](const char c)
        {
            if (c == '\\')
            {
                return '/';
            }

            return c;
        });

    const size_t& firstSeperator = srcPathStr.find_first_of(L'/');
    return { srcPathStr.begin(), srcPathStr.begin() + firstSeperator };
}

static std::wstring GetFirstDirectoryW(const std::filesystem::path& path)
{
    std::wstring srcPathWstr = path.generic_wstring();
    std::transform(srcPathWstr.begin(), srcPathWstr.end(), srcPathWstr.begin(), [](const wchar_t c)
        {
            if (c == L'\\')
            {
                return L'/';
            }

            return c;
        });

    const size_t& firstSeperator = srcPathWstr.find_first_of(L'/');
    return { srcPathWstr.begin(), srcPathWstr.begin() + firstSeperator };
}

static std::filesystem::path GetDestinationPath(std::filesystem::path srcPath)
{
    const std::wstring subDirectory = GetFirstDirectoryW(srcPath);
    const auto& dstPath = std::filesystem::path("HeaderGenerated") / subDirectory / srcPath.replace_extension(".generated.h").filename();

    return dstPath;
}

//----------------------------------------------------------------------------------------------------

std::mutex movedStreamLock;
std::unordered_map<std::filesystem::path, std::fstream> movedStream;

std::mutex postponedFunctionsLock;
std::priority_queue<std::pair<size_t, std::function<void()>>, std::vector<std::pair<size_t, std::function<void()>>>, GreaterComparer> postponedFunctions;

std::mutex bufferedDocumentsLock;
std::vector<std::shared_ptr<rapidjson::Document>> bufferedDocuments;

std::mutex dependenciesLocks;
std::unordered_map<std::string, std::vector<std::string>> dependencies;

//----------------------------------------------------------------------------------------------------

constexpr auto componentTrackingMacroedFileName = "_component_tracking.generated.h";
constexpr auto componentTrackingListFileName = "_component_tracking.generated";

constexpr auto objectTrackingMacroedFileName = "_object_tracking.generated.h";
constexpr auto objectTrackingListFileName = "_object_tracking.generated";

constexpr auto resourceTrackingMacroedFileName = "_resource_tracking.generated.h";
constexpr auto resourceTrackingListFileName = "_resource_tracking.generated";

struct FileNameComparer
{
    bool operator()(const std::pair<std::string, std::filesystem::path>& lhs, const std::pair<std::string, std::filesystem::path>& rhs) const
    {
        return std::less<std::string>{}(lhs.first, rhs.first);
    }
};

enum class TrackingType
{
    Unknown,
    Component,
    Object,
    Resource,
};

struct TrackingContext 
{
    std::mutex trackingLock;
    std::set<std::pair<std::string, std::filesystem::path>, FileNameComparer> trackingLists;
};

TrackingContext componentTracking;
TrackingContext objectTracking;
TrackingContext resourceTracking;

template <TrackingType TrackingT>
constexpr std::string_view GetRegistriationBody()
{
    if constexpr (TrackingT == TrackingType::Component)
    {
        return componentRegistrationBody;
    }
    else if (TrackingT == TrackingType::Object)
    {
        return objectRegistrationBody;
    }
    else if (TrackingT == TrackingType::Resource)
    {
        return resourceRegistrationBody;
    }

    throw std::logic_error("Unknown tracking type");
}

template <TrackingType TrackingT>
constexpr std::string_view GetRegistriationFormat()
{
    if constexpr (TrackingT == TrackingType::Component)
    {
        return componentRegistration;
    }
    else if (TrackingT == TrackingType::Object)
    {
        return objectRegistration;
    }
    else if (TrackingT == TrackingType::Resource)
    {
        return resourceRegistration;
    }

    throw std::logic_error("Unknown tracking type");
}

template <TrackingType TrackingT>
constexpr std::string_view GetUnregistriationFormat()
{
    if constexpr (TrackingT == TrackingType::Component)
    {
        return componentUnregistration;
    }
    else if (TrackingT == TrackingType::Object)
    {
        return objectUnregistration;
    }
    else if (TrackingT == TrackingType::Resource)
    {
        return resourceUnregistration;
    }

    throw std::logic_error("Unknown tracking type");
}
template <TrackingType TrackingT>
static void UpdateLists(
    const char* trackingListFileName, 
    const char* trackingOutputFileName,
    std::set<std::pair<std::string, std::filesystem::path>, FileNameComparer>& trackingList,
    const std::filesystem::path& moduleFilePath)
{
    const std::filesystem::path& subDirectoryOfDst = GetDestinationPath(moduleFilePath).parent_path();

    {
        std::ifstream trackingBundle(subDirectoryOfDst / trackingListFileName);

        if (trackingBundle.is_open())
        {
            std::cout << "Reading from previously generated list file\n";

            while (!trackingBundle.eof())
            {
                std::string fullName;
                std::filesystem::path path;

                trackingBundle >> fullName;
                trackingBundle >> path;

                if (!fullName.empty() && !path.empty())
                {
                    trackingList.emplace(fullName, path);
                }
            }

            trackingBundle.close();
        }
    }

    {
        std::ofstream trackingMacroGeneratedFile(subDirectoryOfDst / trackingOutputFileName);
        if (!trackingMacroGeneratedFile)
        {
            std::cerr << "Could not create the tracking macro file" << '\n';
            return;
        }

        std::stringstream InclusionStream;
        std::stringstream registerMacroStream;
        std::stringstream unregisterMacroStream;

        for (const auto& name : trackingList)
        {
            InclusionStream << std::format("#include \"{0}\"\n", DropFirstDirectory(name.second));
            registerMacroStream << std::format(GetRegistriationFormat<TrackingT>(), name.first);
            unregisterMacroStream << std::format(GetUnregistriationFormat<TrackingT>(), name.first);
        }

        trackingMacroGeneratedFile << std::format(GetRegistriationBody<TrackingT>(), InclusionStream.str(), registerMacroStream.str(), unregisterMacroStream.str());
        trackingMacroGeneratedFile.close();
        std::cout << "tracking macro file has been generated\n";
    }

    {
        std::ofstream trackingBundle(subDirectoryOfDst / trackingListFileName, std::ios::trunc);
        if (!trackingBundle.is_open())
        {
            std::cerr << "Unable to open the generated tracking file\n";
            return;
        }

        for (const auto& pair : trackingList)
        {
            trackingBundle << pair.first << "\n";
            trackingBundle << pair.second << "\n";
        }

        trackingBundle.close();
    }
}

//----------------------------------------------------------------------------------------------------

class postpone_exception 
{
public:
    postpone_exception(const size_t inPriority) : priority(inPriority) {};

    size_t GetPriority() const 
    {
        return priority;
    }

private:
    size_t priority;
};

//----------------------------------------------------------------------------------------------------
void print_usage()
{
  std::cout << "Usage: inputFile" << '\n';
}
//----------------------------------------------------------------------------------------------------

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
            std::cout << "Found property " << (*it)["name"].GetString() << '\n';
            propertyNames.emplace_back((*it)["name"].GetString());
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

        std::cout << std::format("base class scope: {}, class scope: {}, suffix offset: {}", baseClassScope, classScope, classSuffixOffset) <<
            '\n';

        if (baseClassScope == classScope)
        {
            std::cout << "Use class namespace..." << '\n';
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
            std::cout << "Append to class namespace..." << '\n';
	        outBaseClassNamespace.insert(outBaseClassNamespace.begin(), joinedNamespace.begin(), joinedNamespace.end());
            return;
        }

        const size_t parentScopeEnd = joinedNamespace.substr(0, joinedNamespace.size() - 2).rfind("::");
        const size_t parentScopeBegin = joinedNamespace.substr(0, parentScopeEnd + 2).rfind("::");

        // If namespace is one, merge with class namespace.
        if (parentScopeBegin == std::string::npos)
        {
            std::cout << "Class has one namespace, merging..." << '\n';
            outBaseClassNamespace.insert(outBaseClassNamespace.begin(), { ':', ':'});
            outBaseClassNamespace.insert(outBaseClassNamespace.begin(), joinedNamespace.begin(), joinedNamespace.end());
            return;
        }

        std::cout << "Remove the nearest namespace and appending..." << '\n';
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
                    std::cout << "Found class name in the argument, with no namespaces. Append the class namespace..." <<
                        '\n';
                    argumentStream << std::format("{}{}{}", joinedNamespace, (*arg)["name"].GetString(), (arg + 1 == templateArguments.End()) ? "" : ",");
                }
                else
                {
                    argumentStream << std::format("{}{}", (*arg)["name"].GetString(), (arg + 1 == templateArguments.End()) ? "" : ",");
                }
            }
            argumentStream << '>';

            std::cout << "Found template arguments " << argumentStream.str() << '\n';
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

void TestTags(const std::string_view buildConfigurationName, const std::filesystem::path& filePath, std::fstream& outputStream, const std::string_view joinedNamespace, const rapidjson::Value* it, const bool reEntry)
{
    if ((*it)["type"] == "class")
    {
        const std::string closureName = (*it)["name"].GetString();
        std::cout << "Reading closure " << closureName << '\n';
        std::string baseClosure;
        std::string baseClosureNamespace;
        bool        isNativeBaseClass = ReconstructBaseClosureAndNamespace
            (it, joinedNamespace, closureName, baseClosure, baseClosureNamespace);

        std::string closureFullName(joinedNamespace.begin(), joinedNamespace.end());
        closureFullName += closureName;
        std::cout << "Closure parsed with " << closureName << " and " << baseClosureNamespace << baseClosure << '\n';

        std::stringstream additionalHeaders;
        std::stringstream bodyGenerated;
        std::stringstream bodyForStaticGenerated;
        std::stringstream staticsGenerated;
        std::stringstream postGenerated;

        bodyGenerated << std::format(bodyGenerationStaticPrefab, closureFullName, baseClosureNamespace + baseClosure);

        std::string closureType;
        if ((*it)["isstruct"].GetBool() == true)
        {
            closureType = "struct";

            if (it->HasMember("meta"))
            {
                const auto& meta_flag = (*it)["meta"];

                if ( meta_flag.HasMember( "virtual" ) )
                {
                    bodyGenerated << std::format( bodyGenerationOverridablePrefab, closureName );
                }

                if (meta_flag.HasMember("module"))
                {
                    std::cout << "Module found, writing the dependencies\n";
                    std::stringstream dependencyStream;

                    for (const std::string& dependency : dependencies[GetFirstDirectory(filePath)])
                    {
                        dependencyStream << '"' << dependency << '"' << ',';
                    }

                    bodyGenerated << bodyGenerationModuleDecl;
                    postGenerated << std::format(bodyGenerationModuleImpl, closureFullName, dependencyStream.str());
                }

                if (meta_flag.HasMember("clientModule"))
                {
                    if (!reEntry) 
                    {
                        throw postpone_exception(1 << 2);
                    }
                    else 
                    {
                        std::cout << "Module found, writing the dependencies\n";
                        std::stringstream dependencyStream;

                        for (const std::string& dependency : dependencies[GetFirstDirectory(filePath)])
                        {
                            dependencyStream << '"' << dependency << '"' << ',';
                        }
                        
                        bodyGenerated << generatedClientModuleDecl;
                        bodyGenerated << bodyGenerationModuleDecl;
                        postGenerated << std::format(bodyGenerationModuleImpl, closureFullName, dependencyStream.str());
                        postGenerated << std::format("#include \"{0}\"\n", componentTrackingMacroedFileName);
                        postGenerated << std::format("#include \"{0}\"\n", objectTrackingMacroedFileName);
                        postGenerated << std::format("#include \"{0}\"\n", resourceTrackingMacroedFileName);
                        postGenerated << std::format(generatedClientModuleImpl, closureFullName);
                        
                        {
                            std::lock_guard l(componentTracking.trackingLock);
                            UpdateLists<TrackingType::Component>(componentTrackingListFileName, componentTrackingMacroedFileName, componentTracking.trackingLists, filePath);
                        }
                        {
                            std::lock_guard l(objectTracking.trackingLock);
                            UpdateLists<TrackingType::Object>(objectTrackingListFileName, objectTrackingMacroedFileName, objectTracking.trackingLists, filePath);
                        }
                        {
                            std::lock_guard l(resourceTracking.trackingLock);
                            UpdateLists<TrackingType::Resource>(resourceTrackingListFileName, resourceTrackingMacroedFileName, resourceTracking.trackingLists, filePath);
                        }
                    }
                }
            }
        }
        else
        {
            closureType = "class";

            if (it->HasMember("meta"))
            {
                const auto& meta_flag = (*it)["meta"];

                if (meta_flag.HasMember("resource"))
                {
                    bodyGenerated << std::format(bodyGenerationResourceGetter, closureName);

                    if (!meta_flag.HasMember("abstract"))
                    {
                        bodyGenerated << std::format(bodyGenerationResourceCreator, closureName);
                        bodyGenerated << resourceCloneDecl;
                        postGenerated << std::format(resourceCloneImpl, closureFullName);
                        if (!meta_flag["resource"].IsNull() &&
                            !std::strcmp(meta_flag["resource"].GetString(), "client"))
                        {
                            bodyGenerated << "friend struct ConstructorAccess; ";
                            std::lock_guard lock(resourceTracking.trackingLock);
                            resourceTracking.trackingLists.emplace(closureFullName, filePath);
                        }
                    }
                }

                if (meta_flag.HasMember("component"))
                {
                    if (!meta_flag.HasMember("abstract"))
                    {
                        bodyGenerated << componentCloneDecl;
                        postGenerated << std::format(componentCloneImpl, closureFullName);
                        if (!meta_flag["component"].IsNull() &&
                            !std::strcmp(meta_flag["component"].GetString(), "client"))
                        {
                            bodyGenerated << "friend struct ConstructorAccess; ";
                            std::lock_guard lock(componentTracking.trackingLock);
                            componentTracking.trackingLists.emplace(closureFullName, filePath);
                        }
                    }
                }

                if (meta_flag.HasMember("object"))
                {
                    if (!meta_flag.HasMember("abstract"))
                    {
                        bodyGenerated << objectCloneDecl;
                        postGenerated << std::format(objectCloneImpl, closureFullName);
                        if (!meta_flag["object"].IsNull() &&
                            !std::strcmp(meta_flag["object"].GetString(), "client"))
                        {
                            bodyGenerated << "friend struct ConstructorAccess; ";
                            std::lock_guard lock(objectTracking.trackingLock);
                            objectTracking.trackingLists.emplace(closureFullName, filePath);
                        }
                    }
                }   
            }

            bodyGenerated << std::format(bodyGenerationOverridablePrefab, closureName);
        }
        
        if (joinedNamespace.empty()) 
        {
            staticsGenerated << std::format(staticForwardDeclarationWithoutNamespace, closureType, closureName);
        }
        else 
        {
            staticsGenerated << std::format(staticForwardDeclaration, joinedNamespace.substr(0, joinedNamespace.size() - 2), closureType, closureName);
        }

        if (it->HasMember("meta"))
        {
            if ((*it)["meta"].HasMember("serialize"))
            {
                bodyGenerated << GenerateSerializationDeclaration(
                        it, isNativeBaseClass, baseClosureNamespace, baseClosure );

                if ((*it)["meta"].HasMember("abstract"))
                {
                    staticsGenerated << std::format(registerBoostTypeAbstract, closureFullName);
                }
                else
                {
                    staticsGenerated << std::format(registerBoostType, closureFullName);
                }
                postGenerated << std::format(registerBoostTypeImpl, closureFullName);

                staticsGenerated << std::format(registerBoostMetaType, closureFullName);
                postGenerated << std::format(registerBoostMetaTypeImpl, closureFullName);
            }

            if ((*it)["meta"].HasMember("internal"))
            {
                staticsGenerated << std::format(internalTraits, closureFullName);
            }
        }
        
        staticsGenerated << std::format(staticTypePrefab, closureFullName, baseClosureNamespace + baseClosure);

        std::string nameUpperString = closureName;
        std::ranges::transform(nameUpperString, nameUpperString.begin(), [](const char& c){return std::toupper(c);});
        
        outputStream << std::format(generatedHeaderFormat, nameUpperString, postGenerated.str(), additionalHeaders.str(), bodyGenerated.str(), bodyForStaticGenerated.str(), staticsGenerated.str());
    }
}

void RecurseNamespace(const std::string_view buildConfigurationName, const std::filesystem::path& filePath, std::fstream& outputStream, const rapidjson::Value* root, std::vector<std::string_view>& currentNamespace, const bool reEntry)
{
    const rapidjson::Value& ref = *root;

    if (ref["type"] == "namespace")
    {
        currentNamespace.emplace_back(ref["name"].GetString());
        
        if (!ref["members"].Empty())
        {
            for (auto it = ref["members"].End() - 1; it != ref["members"].Begin() - 1; --it)
            {
                if ((*it)["type"] == "namespace")
                {
                    RecurseNamespace(buildConfigurationName, filePath, outputStream, it, currentNamespace, reEntry);
                    continue;
                }

                std::string joinedNamespace;
                for (const std::string_view& identifier : currentNamespace)
                {
                    joinedNamespace += std::format("{}{}", identifier, "::");
                }

                std::cout << "Test header tags with namespace " << joinedNamespace << '\n';
                TestTags(buildConfigurationName, filePath, outputStream, joinedNamespace, it, reEntry);
            }
        }
    }
    else 
    {
        std::string joinedNamespace;
        for (const std::string_view& identifier : currentNamespace)
        {
            joinedNamespace += std::format("{}{}", identifier, "::");
        }

        std::cout << "Test header tags with namespace " << joinedNamespace << '\n';
        TestTags(buildConfigurationName, filePath, outputStream, joinedNamespace, root, reEntry);
    }
}

//----------------------------------------------------------------------------------------------------
int main(int argc, char** argv)
{
  Options options;
  std::string inputFile;
  std::string buildConfigurationName;
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
    ValueArg<std::string> buildName("b", "build", "The name of the build configuration", true, "", "", cmd);

    cmd.parse(argc, argv);

    inputFile = inputFileArg.getValue();
    options.classNameMacro = className.getValue();
    options.enumNameMacro = enumName.getValue();
    options.functionNameMacro = functionName.getValue();
    options.customMacros = customMacro.getValue();
    options.propertyNameMacro = propertyName.getValue();
    options.constructorNameMacro = constructorName.getValue();
    buildConfigurationName = buildName.getValue();
  }
  catch (TCLAP::ArgException& e)
  {
    std::cerr << "error: " << e.error() << " for arg " << e.argId() << '\n';
    return -1;
  }

  std::ifstream inputBundle(inputFile);
  if (!inputBundle.is_open())
  {
      std::cerr << "Could not open " << inputFile << '\n';
      return -1;
  }

  std::vector<std::string> inputFiles;

  std::string readLine;
  while (std::getline(inputBundle, readLine))
  {
      inputFiles.emplace_back(readLine);
  }

  // Open from file
  std::for_each
		  (
		   std::execution::par_unseq, inputFiles.begin(), inputFiles.end(), [&options, &buildConfigurationName](const std::string& filePath)
		   {
			   std::ifstream t(filePath);
			   if (!t.is_open())
			   {
				   std::cerr << "Could not open " << filePath << '\n';
				   return -1;
			   }

               {
                   const std::string subDirectory = GetFirstDirectory(filePath);
                   std::filesystem::path dependencyFileName = subDirectory;
                   dependencyFileName /= (buildConfigurationName + ".dep");
                   std::ifstream dependencyFile(dependencyFileName.generic_string());

                   if (!dependencyFile.is_open())
                   {
                       std::cerr << "Could not open " << dependencyFileName << '\n';
                       return -1;
                   }

                   std::lock_guard lock(dependenciesLocks);
                   if (!dependencies.contains(subDirectory))
                   {
                       std::string readLine;
                       while (std::getline(dependencyFile, readLine))
                       {
                           dependencies[subDirectory].push_back(readLine);
                       }
                   }
               }

			   std::stringstream buffer;
			   buffer << t.rdbuf();

			   Parser parser(options);
			   if (parser.Parse(buffer.str().c_str(), buffer.str().size()))
			   {
                   const std::filesystem::path srcPath = filePath;
                   const std::filesystem::path& dstPath = GetDestinationPath(srcPath);
                   std::cout << dstPath << "\n";
				   if (!exists(dstPath.parent_path()))
				   {
					   std::cout << "Create directory " << dstPath.parent_path() << '\n';
					   create_directories(dstPath.parent_path());
				   }

				   std::fstream outputStream(dstPath.c_str(), std::ios::out);

				   if (!outputStream.is_open())
				   {
					   std::cerr << "Unable to create a file " << dstPath << '\n';
					   return -1;
                   }

				   std::cout << "== Start of " << dstPath << " ==" << '\n';
				   std::cout << "Parsing json" << '\n';
				   std::shared_ptr<rapidjson::Document> document = std::make_shared<rapidjson::Document>();
				   document->Parse(parser.result().c_str());
				   assert(document->IsArray());

				   std::cout << parser.result().c_str() << '\n';

				   try
				   {
                       bool                          postponeTriggered = false;
					   std::vector<std::string_view> queue{};

					   for (auto it = document->End() - 1; it != document->Begin() - 1; --it)
					   {
						   queue.reserve(64);
                           try 
                           {
                               RecurseNamespace(buildConfigurationName, srcPath, outputStream, it, queue, false);
                           }
                           catch (postpone_exception& e)
                           {
                               postponeTriggered = true;
                               std::cout << "Postpone caught\n";

                               {
                                   std::lock_guard documentLock(bufferedDocumentsLock);
                                   bufferedDocuments.emplace_back(document);
                               }

                               {
                                   std::lock_guard lock(postponedFunctionsLock);
                                   postponedFunctions.push({ e.GetPriority(), [srcPath, it, &buildConfigurationName]()
                                       {
                                           std::vector<std::string_view> queue{};
                                           queue.reserve(64);
                                           std::fstream stream;

                                           {
                                               std::lock_guard lock(movedStreamLock);
                                               stream = std::move(movedStream.at(srcPath));
                                           }

                                           RecurseNamespace(buildConfigurationName.c_str(), srcPath, stream, it, queue, true);

                                           if (stream.is_open())
                                           {
                                               stream.close();
                                           }
                                       } });
                               }
                           }
						   queue.clear();
					   }

                       if (postponeTriggered)
                       {
                           std::lock_guard lock(movedStreamLock);
                           movedStream[filePath] = std::move(outputStream);
                       }
				   }
				   catch (std::exception& e)
				   {
					   std::cerr << e.what() << '\n';
                       if (outputStream.is_open())
                       {
                           outputStream.close();
                       }
				   }

                   if (outputStream.is_open())
                   {
                       outputStream.close();
                   }
			   }

            return 0;
		   }
		  );


    if (!postponedFunctions.empty())
    {
        std::cout << "Process remaining postponed\n";

        while (!postponedFunctions.empty())
        {
            const auto func = postponedFunctions.top();
            postponedFunctions.pop();
            func.second();
        }
    }
  
	return 0;
}
