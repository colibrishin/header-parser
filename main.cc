 #include "parser.h"
#include "handler.h"
#include "options.h"
#include "log.h"
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
#include <cctype>
#include <cwctype>
#include <cstdlib>

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

    const size_t firstSeperator = srcPathStr.find_first_of('/');
    if (firstSeperator == std::string::npos)
        return srcPathStr;
    return { srcPathStr.begin() + firstSeperator + 1, srcPathStr.end() };
}

// Path for #include in tracking .generated.h: use path as-is (relative to project source root)
// so e.g. "Components/Public/CubifyComponent.h" is correct; do not drop first segment.
static std::string GetIncludePathForTracking(const std::filesystem::path& path)
{
    std::string s = path.generic_string();
    std::transform(s.begin(), s.end(), s.begin(), [](char c) { return (c == '\\') ? '/' : c; });
    return s;
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

    const size_t firstSeperator = srcPathStr.find_first_of('/');
    if (firstSeperator == std::string::npos)
        return srcPathStr;
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

    const size_t firstSeperator = srcPathWstr.find_first_of(L'/');
    if (firstSeperator == std::wstring::npos)
        return srcPathWstr;
    return { srcPathWstr.begin(), srcPathWstr.begin() + firstSeperator };
}

// Path for #include of the original header in .generated.cpp: preserve full path (e.g. ClientModule/Public/ClientModule)
// so [project.SourceRootPath] finds the real file. Must include Public/Private segment (not just first dir + stem).
static std::string GetIncludePathForOriginalHeader(const std::filesystem::path& srcPath)
{
    std::filesystem::path p = srcPath.generic_string();
    std::string pathStr = (p.parent_path() / p.stem()).generic_string();
    std::transform(pathStr.begin(), pathStr.end(), pathStr.begin(), [](char c) { return (c == '\\') ? '/' : c; });
    return pathStr;
}

// Same Public/Private as GetDestinationPath; use for include paths so tracking headers respect the normal path.
static std::string GetHeaderGeneratedSubdir(const std::filesystem::path& srcPath)
{
    const std::wstring pathStr = srcPath.generic_wstring();
    if (pathStr.find(L"/Public/") != std::wstring::npos)
        return "Public";
    if (pathStr.find(L"/Private/") != std::wstring::npos)
        return "Private";
    const std::wstring firstDir = GetFirstDirectoryW(srcPath);
    return (firstDir.find(L'/') != std::wstring::npos || firstDir == srcPath.generic_wstring()) ? "Public" : std::string(firstDir.begin(), firstDir.end());
}

static std::filesystem::path GetDestinationPath(std::filesystem::path srcPath)
{
    // Emit under HeaderGenerated/Public/ or .../Private/ when source is under Public/ or Private/,
    // so the build's IncludePaths (HeaderGenerated, HeaderGenerated/Public, HeaderGenerated/Private) find the file.
    const std::string subDir = GetHeaderGeneratedSubdir(srcPath);
    const auto& dstPath = std::filesystem::path("HeaderGenerated") / subDir / srcPath.replace_extension(".generated.h").filename();
    return dstPath;
}

//----------------------------------------------------------------------------------------------------

std::mutex movedStreamLock;
std::unordered_map<std::filesystem::path, std::fstream> movedStream;

std::mutex cppStreamsLock;
std::unordered_map<std::filesystem::path, std::stringstream> cppStreamsByFile;

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
            LogOut("Reading from previously generated list file\n");

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
            LogErr("Could not create the tracking macro file\n");
            return;
        }

        std::stringstream InclusionStream;
        std::stringstream registerMacroStream;
        std::stringstream unregisterMacroStream;

        for (const auto& name : trackingList)
        {
            InclusionStream << std::format("#include \"{0}\"\n", GetIncludePathForTracking(name.second));
            registerMacroStream << std::format(GetRegistriationFormat<TrackingT>(), name.first);
            unregisterMacroStream << std::format(GetUnregistriationFormat<TrackingT>(), name.first);
        }

        trackingMacroGeneratedFile << std::format(GetRegistriationBody<TrackingT>(), InclusionStream.str(), registerMacroStream.str(), unregisterMacroStream.str());
        trackingMacroGeneratedFile.close();
        LogOut("tracking macro file has been generated\n");
    }

    {
        std::ofstream trackingBundle(subDirectoryOfDst / trackingListFileName, std::ios::trunc);
        if (!trackingBundle.is_open())
        {
            LogErr("Unable to open the generated tracking file\n");
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
  LogOut("Usage: inputFile\n");
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
            LogOut(std::string("Found property ") + (*it)["name"].GetString() + "\n");
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

        LogOut(std::format("base class scope: {}, class scope: {}, suffix offset: {}\n", baseClassScope, classScope, classSuffixOffset));

        if (baseClassScope == classScope)
        {
            LogOut("Use class namespace...\n");
            // root namescope is same but other than that, every namespaces are different.
            return;
        }

        if (classSuffixOffset != std::string::npos)
        {
            if (const std::string_view classSuffixScope = joinedNamespace.substr(classSuffixOffset);
				baseClassScope == classSuffixScope)
            {
                LogOut("Merge class namespace...");
	            // namespace can be merged.
				outBaseClassNamespace = joinedNamespace;
                return;
            }
        }
        else
        {
            LogOut("Append to class namespace...\n");
	        outBaseClassNamespace.insert(outBaseClassNamespace.begin(), joinedNamespace.begin(), joinedNamespace.end());
            return;
        }

        const size_t parentScopeEnd = joinedNamespace.substr(0, joinedNamespace.size() - 2).rfind("::");
        const size_t parentScopeBegin = joinedNamespace.substr(0, parentScopeEnd + 2).rfind("::");

        // If namespace is one, merge with class namespace.
        if (parentScopeBegin == std::string::npos)
        {
            LogOut("Class has one namespace, merging...\n");
            outBaseClassNamespace.insert(outBaseClassNamespace.begin(), { ':', ':'});
            outBaseClassNamespace.insert(outBaseClassNamespace.begin(), joinedNamespace.begin(), joinedNamespace.end());
            return;
        }

        LogOut("Remove the nearest namespace and appending...\n");
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
                    LogOut("Found class name in the argument, with no namespaces. Append the class namespace...\n");
                    argumentStream << std::format("{}{}{}", joinedNamespace, (*arg)["name"].GetString(), (arg + 1 == templateArguments.End()) ? "" : ",");
                }
                else
                {
                    argumentStream << std::format("{}{}", (*arg)["name"].GetString(), (arg + 1 == templateArguments.End()) ? "" : ",");
                }
            }
            argumentStream << '>';

            LogOut(std::string("Found template arguments ") + argumentStream.str() + "\n");
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

void TestTags(const std::string_view buildConfigurationName, const std::filesystem::path& filePath, std::fstream& outputStream, std::ostream& cppStream, const std::string_view joinedNamespace, const rapidjson::Value* it, const bool reEntry)
{
    if ((*it)["type"] == "class")
    {
        const std::string closureName = (*it)["name"].GetString();
        LogOut(std::string("Reading closure ") + closureName + "\n");
        std::string baseClosure;
        std::string baseClosureNamespace;
        bool        isNativeBaseClass = ReconstructBaseClosureAndNamespace
            (it, joinedNamespace, closureName, baseClosure, baseClosureNamespace);

        std::string closureFullName(joinedNamespace.begin(), joinedNamespace.end());
        closureFullName += closureName;
        LogOut(std::format("Closure parsed with {} and {}{}\n", closureName, baseClosureNamespace, baseClosure));

        std::stringstream additionalHeaders;
        std::stringstream bodyGenerated;
        std::stringstream bodyForStaticGenerated;
        std::stringstream staticsGenerated;
        std::stringstream postGenerated;

        // Postpone clientModule before writing so we only emit body/cpp once on re-entry (avoids duplicate StaticIsDerivedOf etc.).
        if ((*it)["isstruct"].GetBool() == true && it->HasMember("meta"))
        {
            const auto& meta_flag = (*it)["meta"];
            if (meta_flag.HasMember("clientModule") && !reEntry)
                throw postpone_exception(1 << 2);
        }

        bodyGenerated << std::format(bodyGenerationStaticPrefab, closureFullName, baseClosureNamespace + baseClosure);
        cppStream << std::format(bodyGenerationStaticIsDerivedOfDef, closureFullName);

        std::string closureType;
        if ((*it)["isstruct"].GetBool() == true)
        {
            closureType = "struct";

            if (it->HasMember("meta"))
            {
                const auto& meta_flag = (*it)["meta"];

                if ( meta_flag.HasMember( "virtual" ) )
                {
                    bodyGenerated << std::format( bodyGenerationOverridablePrefabDecl, closureName );
                    cppStream << std::format( bodyGenerationOverridablePrefabDef, closureFullName );
                }

                if (meta_flag.HasMember("module"))
                {
                    LogOut("Module found, writing the dependencies\n");
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
                    // reEntry only: we throw before writing on first pass (see early check above).
                    {
                        LogOut("Module found, writing the dependencies\n");
                        std::stringstream dependencyStream;

                        for (const std::string& dependency : dependencies[GetFirstDirectory(filePath)])
                        {
                            dependencyStream << '"' << dependency << '"' << ',';
                        }
                        
                        bodyGenerated << generatedClientModuleDecl;
                        bodyGenerated << bodyGenerationModuleDecl;
                        postGenerated << std::format(bodyGenerationModuleImpl, closureFullName, dependencyStream.str());
                        // Generated header include path (HeaderGenerated[/Public|/Private]) is set by Sharpmake; use filename only.
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
                    additionalHeaders << "#include \"ResourceManager.h\"\n"
                                      << "#include \"CoreMacro.h\"\n";
                    bodyGenerated << std::format(bodyGenerationResourceGetter, closureName);
                    postGenerated << std::format(bodyGenerationResourceBaseAssert, closureFullName);

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

            bodyGenerated << std::format(bodyGenerationOverridablePrefabDecl, closureName);
            cppStream << std::format(bodyGenerationOverridablePrefabDef, closureFullName);
        }
        
        if (joinedNamespace.empty()) 
        {
            staticsGenerated << std::format(staticForwardDeclarationWithoutNamespace, closureType, closureName);
        }
        else 
        {
            staticsGenerated << std::format(staticForwardDeclaration, joinedNamespace.substr(0, joinedNamespace.size() - 2), closureType, closureName);
        }
        staticsGenerated << std::format(staticTypePrefab, closureFullName, baseClosureNamespace + baseClosure);

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
        
        std::string nameUpperString = closureName;
        std::ranges::transform(nameUpperString, nameUpperString.begin(), [](const char& c){return std::toupper(c);});

        // Emit implementation (s_dependencies_, cloneImpl, BOOST exports) to .generated.cpp; generatedHeaderFormat has no {1}.
        cppStream << postGenerated.str();

        outputStream << std::format(generatedHeaderFormat,
            nameUpperString,
            postGenerated.str(),
            additionalHeaders.str(),
            bodyGenerated.str(),
            bodyForStaticGenerated.str(),
            staticsGenerated.str());
    }
}

void RecurseNamespace(const std::string_view buildConfigurationName, const std::filesystem::path& filePath, std::fstream& outputStream, std::ostream& cppStream, const rapidjson::Value* root, std::vector<std::string_view>& currentNamespace, const bool reEntry)
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
                    RecurseNamespace(buildConfigurationName, filePath, outputStream, cppStream, it, currentNamespace, reEntry);
                    continue;
                }

                std::string joinedNamespace;
                for (const std::string_view& identifier : currentNamespace)
                {
                    joinedNamespace += std::format("{}{}", identifier, "::");
                }

                LogOut(std::string("Test header tags with namespace ") + std::string(joinedNamespace) + "\n");
                TestTags(buildConfigurationName, filePath, outputStream, cppStream, joinedNamespace, it, reEntry);
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

        LogOut(std::string("Test header tags with namespace ") + joinedNamespace + "\n");
        TestTags(buildConfigurationName, filePath, outputStream, cppStream, joinedNamespace, root, reEntry);
    }
}

//----------------------------------------------------------------------------------------------------
int main(int argc, char** argv)
{
  StartLogThread();

  Options options;
  std::string inputFile;
  std::string buildConfigurationName;
  unsigned int maxJobs = 0;
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
    ValueArg<std::string> maxJobsArg("j", "max-jobs", "Max parallel file jobs (1=sequential, 0=default parallel). Env HEADER_PARSER_MAX_JOBS overrides.", false, "1", "", cmd);

    cmd.parse(argc, argv);

    if (const char* env = std::getenv("HEADER_PARSER_MAX_JOBS"))
    {
        try { maxJobs = static_cast<unsigned int>(std::stoul(env)); }
        catch (...) {}
    }
    if (maxJobs == 0)
    {
        try { maxJobs = static_cast<unsigned int>(std::stoul(maxJobsArg.getValue())); }
        catch (...) {}
    }

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
    LogErr(std::string("error: ") + e.error() + " for arg " + e.argId() + "\n");
    StopLogThread();
    return -1;
  }

  std::ifstream inputBundle(inputFile);
  if (!inputBundle.is_open())
  {
      LogErr(std::string("[header-parser] Could not open target file: ") + inputFile + "\n");
      StopLogThread();
      return -1;
  }

  std::vector<std::string> inputFiles;

  std::string readLine;
  while (std::getline(inputBundle, readLine))
  {
      // Trim CRLF / trailing whitespace (balius writeln! on Windows writes \r\n)
      while (!readLine.empty() && (readLine.back() == '\r' || readLine.back() == '\n' || std::isspace(static_cast<unsigned char>(readLine.back()))))
          readLine.pop_back();
      if (readLine.empty())
          continue;
      inputFiles.emplace_back(readLine);
  }

  // Open from file (sequential when --max-jobs=1 or HEADER_PARSER_MAX_JOBS=1 to reduce thread count)
  auto processOneFile = [&options, &buildConfigurationName](const std::string& filePath)
  {
			   std::ifstream t(filePath);
			   if (!t.is_open())
			   {
				   LogErr(std::string("[header-parser] Could not open header: ") + filePath + "\n");
				   return -1;
			   }

               {
                   const std::string subDirectory = GetFirstDirectory(filePath);
                   std::lock_guard lock(dependenciesLocks);
                   if (!dependencies.contains(subDirectory))
                   {
                       std::filesystem::path dependencyFileName = subDirectory;
                       dependencyFileName /= (buildConfigurationName + ".dep");
                       std::ifstream dependencyFile(dependencyFileName.generic_string());
                       if (dependencyFile.is_open())
                       {
                           std::string readLine;
                           while (std::getline(dependencyFile, readLine))
                           {
                               while (!readLine.empty() && (readLine.back() == '\r' || readLine.back() == '\n' || std::isspace(static_cast<unsigned char>(readLine.back()))))
                                   readLine.pop_back();
                               if (!readLine.empty())
                                   dependencies[subDirectory].push_back(readLine);
                           }
                       }
                       // If .dep is missing, leave dependencies[subDirectory] empty so module s_dependencies_ is still generated as {}.
                   }
               }

			   std::stringstream buffer;
			   buffer << t.rdbuf();

			   Parser parser(options);
			   if (parser.Parse(buffer.str().c_str(), buffer.str().size()))
			   {
                   const std::filesystem::path srcPath = filePath;
                   const std::filesystem::path& dstPath = GetDestinationPath(srcPath);
				   if (!exists(dstPath.parent_path()))
					   create_directories(dstPath.parent_path());

				   std::fstream outputStream(dstPath.c_str(), std::ios::out);

				   if (!outputStream.is_open())
				   {
					   LogErr(std::string("[header-parser] Unable to create output: ") + dstPath.generic_string() + "\n");
					   return -1;
                   }

				   std::shared_ptr<rapidjson::Document> document = std::make_shared<rapidjson::Document>();
				   document->Parse(parser.result().c_str());
				   assert(document->IsArray());

				   std::ostream* pCppStream = nullptr;
				   {
					   std::lock_guard lock(cppStreamsLock);
					   cppStreamsByFile[srcPath].str("");
					   cppStreamsByFile[srcPath].clear();
					   pCppStream = &cppStreamsByFile[srcPath];
				   }

				   try
				   {
                       bool                          postponeTriggered = false;
					   std::vector<std::string_view> queue{};

					   for (auto it = document->End() - 1; it != document->Begin() - 1; --it)
					   {
						   queue.reserve(64);
                           try 
                           {
                               RecurseNamespace(buildConfigurationName, srcPath, outputStream, *pCppStream, it, queue, false);
                           }
                           catch (postpone_exception& e)
                           {
                               postponeTriggered = true;

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
                                           std::ostream* pCpp = nullptr;
                                           {
                                               std::lock_guard lock(movedStreamLock);
                                               stream = std::move(movedStream.at(srcPath));
                                           }
                                           {
                                               std::lock_guard lock(cppStreamsLock);
                                               pCpp = &cppStreamsByFile[srcPath];
                                           }

                                           RecurseNamespace(buildConfigurationName.c_str(), srcPath, stream, *pCpp, it, queue, true);

                                           if (stream.is_open())
                                           {
                                               stream.close();
                                           }

                                           const auto cppPath = GetDestinationPath(srcPath).parent_path() / (srcPath.stem().string() + ".generated.cpp");
                                           std::ofstream cppFile(cppPath);
                                           if (cppFile.is_open())
                                           {
                                               std::string cppContent;
                                               { std::lock_guard lock(cppStreamsLock); cppContent = cppStreamsByFile[srcPath].str(); }
                                               cppFile << std::format(generatedCppFormat, GetIncludePathForOriginalHeader(srcPath), srcPath.stem().string(), cppContent);
                                               cppFile.close();
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
					   else
					   {
						   const auto cppPath = GetDestinationPath(srcPath).parent_path() / (srcPath.stem().string() + ".generated.cpp");
						   std::ofstream cppFile(cppPath);
						   if (cppFile.is_open())
						   {
							   std::string cppContent;
							   { std::lock_guard lock(cppStreamsLock); cppContent = cppStreamsByFile[srcPath].str(); }
							   cppFile << std::format(generatedCppFormat, GetIncludePathForOriginalHeader(srcPath), srcPath.stem().string(), cppContent);
							   cppFile.close();
						   }
					   }
				   }
				   catch (std::exception& e)
				   {
					   LogErr(std::string("[header-parser] Exception ") + filePath + ": " + e.what() + "\n");
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
			   else
			   {
			       LogErr(std::string("[header-parser] Parse failed: ") + filePath + "\n");
			   }

            return 0;
  };
  if (maxJobs == 1)
    std::for_each(std::execution::seq, inputFiles.begin(), inputFiles.end(), processOneFile);
  else
    std::for_each(std::execution::par_unseq, inputFiles.begin(), inputFiles.end(), processOneFile);

    if (!postponedFunctions.empty())
    {

        while (!postponedFunctions.empty())
        {
            const auto func = postponedFunctions.top();
            postponedFunctions.pop();
            func.second();
        }
    }

  StopLogThread();
	return 0;
}
