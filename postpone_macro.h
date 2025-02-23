#pragma once

constexpr std::string_view componentRegistrationBody =
"#pragma once\n"
"#include \"Component/Public/Component.h\"\n"
"{0}\n"
"#undef CLINET_COMPONENT_REGISTRATION\n"
"#undef CLINET_COMPONENT_UNREGISTRATION\n"
"#define CLINET_COMPONENT_REGISTRATION {1}\n"
"#define CLINET_COMPONENT_UNREGISTRATION {2}";

constexpr std::string_view generatedClientModuleDecl = "void GeneratedInitialize() override; void GeneratedShutdown() override;\\\n";
constexpr std::string_view generatedClientModuleImpl = 
"void {0}::GeneratedInitialize() {{ CLINET_COMPONENT_REGISTRATION CLINET_OBJECT_REGISTRATION CLINET_RESOURCE_REGISTRATION }} "
"void {0}::GeneratedShutdown() {{ CLINET_COMPONENT_UNREGISTRATION CLINET_OBJECT_UNREGISTRATION CLINET_RESOURCE_UNREGISTRATION }}";

constexpr std::string_view componentRegistration = "Engine::ComponentFactory::Register<{0}>();";
constexpr std::string_view componentUnregistration = "Engine::ComponentFactory::Unregister<{0}>();";

constexpr std::string_view objectRegistrationBody =
"#pragma once\n"
"#include \"ObjectBase/Public/ObjectBase.h\"\n"
"{0}\n"
"#undef CLINET_OBJECT_REGISTRATION\n"
"#undef CLINET_OBJECT_UNREGISTRATION\n"
"#define CLINET_OBJECT_REGISTRATION {1}\n"
"#define CLINET_OBJECT_UNREGISTRATION {2}";

constexpr std::string_view objectRegistration = "Engine::ObjectFactory::Register<{0}>();";
constexpr std::string_view objectUnregistration = "Engine::ObjectFactory::Unregister<{0}>();";

constexpr std::string_view resourceRegistrationBody =
"#pragma once\n"
"#include \"Resource/Public/Resource.h\"\n"
"{0}\n"
"#undef CLINET_RESOURCE_REGISTRATION\n"
"#undef CLINET_RESOURCE_UNREGISTRATION\n"
"#define CLINET_RESOURCE_REGISTRATION {1}\n"
"#define CLINET_RESOURCE_UNREGISTRATION {2}";

constexpr std::string_view resourceRegistration = "Engine::ResourceFactory::Register<{0}>();";
constexpr std::string_view resourceUnregistration = "Engine::ResourceFactory::Unregister<{0}>();";