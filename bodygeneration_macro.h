#pragma once
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
"{4}"
"#ifdef GENERATE_BODY\n"
"#undef GENERATE_BODY\n"
"#endif\n"
"#define GENERATE_BODY {1}\n"
"{2}\n"
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

constexpr auto resourceCloneDecl = "protected: RES_CLONE_DECL public: ";
constexpr auto resourceCloneImpl = "RES_CLONE_IMPL( {0} )\n";

constexpr auto scriptCloneDecl = "protected: SCRIPT_CLONE_DECL public: ";
constexpr auto scriptCloneImpl = "SCRIPT_CLONE_IMPL( {0} )\n";

constexpr auto serializeInlineDeclStart =
"friend class Engine::Serializer; friend class boost::serialization::access; private: template <class Archive> void serialize(Archive &ar, const unsigned int /*file_version*/) {";
constexpr auto serializeBaseClassAr = "ar& boost::serialization::base_object<{0}>(*this); ";
constexpr auto serializePropertyAr = "ar& {0}; ";
constexpr auto serializeInlineDeclEnd = "} public: ";

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
"virtual bool IsDerivedOf(HashType base) const {{ return {0}::StaticIsDerivedOf(base); }}"
"virtual bool IsBaseOf(HashType derived) const {{ return derived->IsDerivedOf({0}::StaticTypeHash()); }}";

constexpr auto bodyGenerationResourceGetter =
"template <typename Void = void, typename Name> requires (std::is_base_of_v<Engine::Abstracts::Resource, {0}>, std::is_constructible_v<std::string_view, Name>)\
static Engine::Weak<{0}> Get(const Name& name) {{ return Engine::Managers::ResourceManager::GetInstance().GetResource<{0}>(name); }}\
template <typename Void = void, typename MetaPath> requires (std::is_base_of_v<Engine::Abstracts::Resource, {0}>, std::is_constructible_v<std::filesystem::path, MetaPath>)\
static Engine::Weak<{0}> GetByMetadataPath(const MetaPath& meta_path) {{ return Engine::Managers::ResourceManager::GetInstance().GetResourceByMetadataPath<{0}>(meta_path); }} ";

constexpr auto bodyGenerationResourceCreator =
"template <typename Name, typename RawPath, typename... Args> requires (\
std::is_base_of_v<Engine::Abstracts::Resource, {0}>,\
std::is_constructible_v<std::string_view, Name>,\
std::is_constructible_v<std::filesystem::path, RawPath>,\
std::is_constructible_v<{0}, RawPath, Args...>)\
static Engine::Strong<{0}> Create(const Name& name, const RawPath& raw_path, Args&&... args)\
{{\
const std::string_view name_view(name);\
const std::filesystem::path path_view(raw_path);\
if (const auto& name_wise = {0}::Get(name_view).lock(); !name_view.empty() && name_wise) {{\
return name_wise; }}\
const auto obj = boost::shared_ptr<{0}>(new {0}(path_view, std::forward<Args>(args)...));\
Engine::Managers::ResourceManager::GetInstance().AddResource(name_view, obj);\
obj->Load();\
Engine::Serializer::Serialize(obj->GetName(), obj);\
return obj; \
}}\
template <typename Name, typename... Args> requires (\
std::is_base_of_v<Engine::Abstracts::Resource, {0}>,\
std::is_constructible_v<std::string_view, Name>,\
!std::is_constructible_v<{0}, std::filesystem::path, Args...>)\
static Engine::Strong<{0}> Create(const Name& name, Args&&... args)\
{{\
const std::string_view name_view(name);\
if (const auto& name_wise = Engine::Managers::ResourceManager::GetInstance().GetResource<{0}>(name_view).lock(); !name_view.empty() && name_wise) {{ return name_wise; }}\
const auto obj = boost::shared_ptr<{0}>(new {0}(std::forward<Args>(args)...));\
Engine::Managers::ResourceManager::GetInstance().AddResource(name_view, obj);\
obj->Load();\
if constexpr (is_serializable_v<{0}>) {{\
Engine::Serializer::Serialize(obj->GetName(), obj);\
}}\
return obj; \
}} ";

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