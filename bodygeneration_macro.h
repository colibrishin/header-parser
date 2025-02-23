#pragma once
constexpr auto generatedHeaderFormat = "#ifdef {0}_GENERATED_H\n"
                                       "#ifndef POST_{0}_H\n"
                                       "#define POST_{0}_H\n"
                                       "{1}\n"
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
                                       "{2}"
                                       "#ifdef GENERATE_BODY\n"
                                       "#undef GENERATE_BODY\n"
                                       "#endif\n"
                                       "#ifdef GENERATE_BODY_HEAD\n"
                                       "#undef GENERATE_BODY_HEAD\n"
                                       "#endif\n"
                                       "#ifdef GENERATE_BODY_STATIC\n"
                                       "#undef GENERATE_BODY_STATIC\n"
                                       "#endif\n"
                                       "#define GENERATE_BODY GENERATE_BODY_HEAD GENERATE_BODY_STATIC\n"
                                       "#define GENERATE_BODY_HEAD {3}\n"
                                       "#if !IS_DLL\n"
                                       "#define GENERATE_BODY_STATIC {4}\n"
                                       "#else\n"
                                       "#define GENERATE_BODY_STATIC\n"
                                       "#endif\n"
                                       "{5}\n"
                                       "#endif\n";

constexpr auto staticForwardDeclaration = "namespace {0} {{{1} {2};}}\n";
constexpr auto staticForwardDeclarationWithoutNamespace = "{0} {1};\n";

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
constexpr static bool is_derived_of(const HashType base) \
{{ \
if constexpr ((upcast_count * sizeof(HashTypeValue)) < (1 << 7)) \
{{ \
return std::ranges::find_if(upcast_array, [&base](const auto other){{return base->Equal(*other);}}) != upcast_array.end(); \
}} \
return std::ranges::binary_search(upcast_array, base, [](const auto lhs, const auto rhs){{return *lhs < *rhs;}}); \
}} \
}};\n";

constexpr auto internalTraits =
"template <> struct is_internal<{0}> : public std::true_type {{}};\n";

constexpr auto resourceCloneDecl = "protected: RES_CLONE_DECL public:\\\n";
constexpr auto resourceCloneImpl = "RES_CLONE_IMPL( {0} )\n";

constexpr auto objectCloneDecl = "protected: OBJ_CLONE_DECL public:\\\n";
constexpr auto objectCloneImpl = "OBJ_CLONE_IMPL( {0} )\n";

constexpr auto componentCloneDecl = "protected: COMP_CLONE_DECL public:\\\n";
constexpr auto componentCloneImpl = "COMP_CLONE_IMPL( {0} )\n";

constexpr auto serializeInlineDeclStart =
"friend class Engine::Serializer;\\\n"
"friend class boost::serialization::access;\\\n"
"private:\\\n"
"template <class Archive> void serialize(Archive &ar, const unsigned int /*file_version*/) {\\\n";
constexpr auto serializeBaseClassAr = "ar& boost::serialization::base_object<{0}>(*this);\\\n";
constexpr auto serializePropertyAr = "ar& {0};\\\n";
constexpr auto serializeInlineDeclEnd = "} public:\\\n";

constexpr auto bodyGenerationStaticPrefab = "public: typedef {1} Base;\\\n"
                                            "constexpr static std::string_view StaticTypeName()\\\n"
                                            "{{\\\n"
                                            "return static_type_name<{0}>::name();\\\n"
                                            "}}\\\n"
                                            "constexpr static std::string_view StaticFullTypeName()\\\n"
                                            "{{\\\n"
                                            "return static_type_name<{0}>::full_name();\\\n"
                                            "}}\\\n"
                                            "constexpr static HashType StaticTypeHash()\\\n"
                                            "{{\\\n"
                                            "return &type_hash<{0}>::value;\\\n"
                                            "}}\\\n"
                                            "constexpr static bool StaticIsDerivedOf(HashType hash)\\\n"
                                            "{{\\\n"
                                            "return polymorphic_type_hash<{0}>::is_derived_of(hash);\\\n"
                                            "}}\\\n";

constexpr auto bodyGenerationOverridablePrefab =
        "virtual std::string_view GetTypeName() const {{ return {0}::StaticFullTypeName(); }}\\\n"
        "virtual std::string_view GetPrettyTypeName() const {{ return {0}::StaticTypeName(); }}\\\n"
        "virtual HashType GetTypeHash() const {{ return {0}::StaticTypeHash(); }}\\\n"
        "virtual bool IsDerivedOf(HashType base) const {{ return {0}::StaticIsDerivedOf(base); }}\\\n"
        "virtual bool IsBaseOf(HashType derived) const {{ return derived->IsDerivedOf({0}::StaticTypeHash()); }}\\\n";

constexpr auto bodyGenerationResourceGetter =
        "template <typename Void = void, typename Name> requires (std::is_base_of_v<Engine::Abstracts::Resource, {0}>, "
        "std::is_constructible_v<std::string_view, Name>)\\\n"
        "static Engine::Weak<{0}> Get(const Name& name) {{ return "
        "Engine::Managers::ResourceManager::GetInstance().GetResource<{0}>(name); }}\\\n"
        "template <typename Void = void, typename MetaPath> requires (std::is_base_of_v<Engine::Abstracts::Resource, "
        "{0}>, std::is_constructible_v<std::filesystem::path, MetaPath>)\\\n"
        "static Engine::Weak<{0}> GetByMetadataPath(const MetaPath& meta_path) {{ return "
        "Engine::Managers::ResourceManager::GetInstance().GetResourceByMetadataPath<{0}>(meta_path); }}\\\n";

constexpr auto bodyGenerationResourceCreator =
        "template <typename Name, typename RawPath, typename... Args> requires (\\\n"
        "std::is_base_of_v<Engine::Abstracts::Resource, {0}>,\\\n"
        "std::is_constructible_v<std::string_view, Name>,\\\n"
        "std::is_constructible_v<std::filesystem::path, RawPath>,\\\n"
        "std::is_constructible_v<{0}, RawPath, Args...>)\\\n"
        "static Engine::Strong<{0}> Create(const Name& name, const RawPath& raw_path, Args&&... args)\\\n"
        "{{\\\n"
        "const std::string_view name_view(name);\\\n"
        "const std::filesystem::path path_view(raw_path);\\\n"
        "if (const auto& name_wise = {0}::Get(name_view).lock(); !name_view.empty() && name_wise) {{\\\n"
        "return name_wise; }}\\\n"
        "const auto obj = boost::shared_ptr<{0}>(new {0}(path_view, std::forward<Args>(args)...));\\\n"
        "Engine::Managers::ResourceManager::GetInstance().AddResource(name_view, obj);\\\n"
        "obj->Load();\\\n"
        "Engine::Serializer::Serialize(obj->GetName(), obj);\\\n"
        "return obj;\\\n"
        "}}\\\n"
        "template <typename Name, typename... Args> requires (\\\n"
        "std::is_base_of_v<Engine::Abstracts::Resource, {0}>,\\\n"
        "std::is_constructible_v<std::string_view, Name>,\\\n"
        "!std::is_constructible_v<{0}, std::filesystem::path, Args...>)\\\n"
        "static Engine::Strong<{0}> Create(const Name& name, Args&&... args)\\\n"
        "{{\\\n"
        "const std::string_view name_view(name);\\\n"
        "if (const auto& name_wise = "
        "Engine::Managers::ResourceManager::GetInstance().GetResource<{0}>(name_view).lock(); !name_view.empty() && "
        "name_wise) {{ return name_wise; }}\\\n"
        "const auto obj = boost::shared_ptr<{0}>(new {0}(std::forward<Args>(args)...));\\\n"
        "Engine::Managers::ResourceManager::GetInstance().AddResource(name_view, obj);\\\n"
        "obj->Load();\\\n"
        "if constexpr (is_serializable_v<{0}>) {{\\\n"
        "Engine::Serializer::Serialize(obj->GetName(), obj);\\\n"
        "}}\\\n"
        "return obj;\\\n"
        "}}\\\n";

constexpr auto bodyGenerationModuleDecl =
        "private: static const std::vector<std::string> s_dependencies_;\\\n"
        "public: const std::vector<std::string>& GetDependencies() const {{ return s_dependencies_; }} \n";

constexpr auto registerBoostType =
"template <> struct is_serializable<{0}> : public std::true_type {{}};\n"
"BOOST_CLASS_EXPORT_KEY({0})\n";

constexpr auto registerBoostTypeAbstract =
"template <> struct is_serializable<{0}> : public std::true_type {{}};\n"
"BOOST_SERIALIZATION_ASSUME_ABSTRACT({0})\n";

constexpr auto registerBoostTypeImpl =
"BOOST_CLASS_EXPORT_IMPLEMENT({0})\n";

constexpr auto registerBoostMetaType =
"BOOST_CLASS_EXPORT_KEY(HashTypeT<{0}>)\n";

constexpr auto registerBoostMetaTypeImpl =
"BOOST_CLASS_EXPORT_IMPLEMENT(HashTypeT<{0}>)\n";