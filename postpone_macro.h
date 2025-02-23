#pragma once

constexpr std::string_view componentRegistrationBody =
"#pragma once\n"
"#include \"Component.h\"\n"
"{0}\n"
"#undef CLIENT_COMPONENT_REGISTRATION\n"
"#undef CLIENT_COMPONENT_UNREGISTRATION\n"
"#define CLIENT_COMPONENT_REGISTRATION {1}\n"
"#define CLIENT_COMPONENT_UNREGISTRATION {2}";

constexpr std::string_view generatedClientModuleDecl = "void GeneratedInitialize() override; void GeneratedShutdown() override;\\\n";
constexpr std::string_view generatedClientModuleImpl = 
"void {0}::GeneratedInitialize() {{ CLIENT_COMPONENT_REGISTRATION CLIENT_OBJECT_REGISTRATION CLIENT_RESOURCE_REGISTRATION }} "
"void {0}::GeneratedShutdown() {{ CLIENT_COMPONENT_UNREGISTRATION CLIENT_OBJECT_UNREGISTRATION CLIENT_RESOURCE_UNREGISTRATION }}";

constexpr std::string_view componentRegistration = "Engine::ComponentFactory::Register<{0}>();";
constexpr std::string_view componentUnregistration = "Engine::ComponentFactory::Unregister<{0}>();";

constexpr std::string_view objectRegistrationBody =
"#pragma once\n"
"#include \"ObjectBase.h\"\n"
"{0}\n"
"#undef CLIENT_OBJECT_REGISTRATION\n"
"#undef CLIENT_OBJECT_UNREGISTRATION\n"
"#define CLIENT_OBJECT_REGISTRATION {1}\n"
"#define CLIENT_OBJECT_UNREGISTRATION {2}";

constexpr std::string_view objectRegistration = "Engine::ObjectFactory::Register<{0}>();";
constexpr std::string_view objectUnregistration = "Engine::ObjectFactory::Unregister<{0}>();";

constexpr std::string_view resourceRegistrationBody =
"#pragma once\n"
"#include \"Resource.h\"\n"
"{0}\n"
"#undef CLIENT_RESOURCE_REGISTRATION\n"
"#undef CLIENT_RESOURCE_UNREGISTRATION\n"
"#define CLIENT_RESOURCE_REGISTRATION {1}\n"
"#define CLIENT_RESOURCE_UNREGISTRATION {2}";

constexpr std::string_view resourceRegistration = "Engine::ResourceFactory::Register<{0}>();";
constexpr std::string_view resourceUnregistration = "Engine::ResourceFactory::Unregister<{0}>();";