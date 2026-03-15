#pragma once
constexpr auto generatedHeaderFormat = "#ifndef {0}_GENERATED_H\n"
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
                                       "#define GENERATE_BODY GENERATE_BODY_HEAD\n"
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
"template <> struct polymorphic_type_hash<{0}>\n\
{{\n\
using chain = type_list_prepend<{0}, polymorphic_type_hash<{1}>::chain>::type;\n\
static constexpr size_t upcast_count = type_list_size<chain>::value;\n\
static constexpr HashArray<upcast_count> upcast_array = chain_to_upcast_array<chain>::value;\n\
constexpr static bool is_derived_of(const HashType base)\n\
{{\n\
return std::ranges::find_if(upcast_array, [&base](const HashType other){{return other && base && (other == base || other->v == base->v);}}) != upcast_array.end();\n\
}}\n\
}};\n";

// Declaration only: so the generated header does not define the specialization in every TU.
constexpr auto polymorphicTypeHashDecl = "template <> struct polymorphic_type_hash<{0}>;\n";

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

constexpr auto bodyGenerationStaticPrefab = "friend struct ConstructorAccess;\\\n"
                                            "public: typedef {1} Base;\\\n"
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
                                            "constexpr static bool StaticIsDerivedOf(HashType hash);\\\n";
constexpr auto bodyGenerationStaticIsDerivedOfDef =
        "constexpr bool {0}::StaticIsDerivedOf(HashType hash) {{ return polymorphic_type_hash<{0}>::is_derived_of(hash); }}\n";

// Declaration-only: expanded in class in header. Definitions go to .generated.cpp.
constexpr auto bodyGenerationOverridablePrefabDecl =
        "virtual std::string_view GetTypeName() const;\\\n"
        "virtual std::string_view GetPrettyTypeName() const;\\\n"
        "virtual HashType GetTypeHash() const;\\\n"
        "virtual bool IsDerivedOf(HashType base) const;\\\n"
        "virtual bool IsBaseOf(HashType derived) const;\\\n"
        "virtual AllocationContext GetAllocationContext() const;\\\n";

constexpr auto bodyGenerationOverridablePrefabDef =
        "std::string_view {0}::GetTypeName() const {{ return {0}::StaticFullTypeName(); }}\n"
        "std::string_view {0}::GetPrettyTypeName() const {{ return {0}::StaticTypeName(); }}\n"
        "HashType {0}::GetTypeHash() const {{ return {0}::StaticTypeHash(); }}\n"
        "bool {0}::IsDerivedOf(HashType base) const {{ return {0}::StaticIsDerivedOf(base); }}\n"
        "bool {0}::IsBaseOf(HashType derived) const {{ return derived->IsDerivedOf({0}::StaticTypeHash()); }}\n"
        "AllocationContext {0}::GetAllocationContext() const {{ return g_allocator_storage.get_allocator<{0}>().get_context(static_cast<const {0}*>(this)); }}\n";

// Type limitation for resource base: enforced in .generated.cpp (where class is complete) to avoid __is_base_of on incomplete type in header.
constexpr auto bodyGenerationResourceBaseAssert =
        "static_assert(std::is_base_of_v<Engine::Abstracts::Resource, {0}>);\n";

// Resource getter: use Resource.h macro so the getter is defined in one place.
constexpr auto bodyGenerationResourceGetter = "RESOURCE_SELF_INFER_GETTER({0})\\\n";

// Resource creator: use Resource.h macro so both Create overloads are defined in one place.
constexpr auto bodyGenerationResourceCreator = "RESOURCE_SELF_INFER_CREATE({0})\\\n";

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

// .generated.cpp: include original header (class definition), then generated header; implementations follow.
// {0} = include path for original header (e.g. "Public/ClientModule" so [project.SourceRootPath] finds it).
// {1} = stem only for .generated.h (same dir as .cpp). {2} = cpp content.
constexpr auto generatedCppFormat =
"// Generated by header-parser - do not edit\n"
"#include \"{0}.h\"\n"
"#include \"{1}.generated.h\"\n"
"\n"
"{2}\n";
