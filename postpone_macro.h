#pragma once

constexpr std::string_view scriptRegistrationBody =
"#pragma once\n"
"#include \"Script/Public/Script.h\"\n"
"{0}\n"
"#undef CLINET_SCRIPT_REGISTRATION\n"
"#undef CLINET_SCRIPT_UNREGISTRATION\n"
"#define CLINET_SCRIPT_REGISTRATION {1}\n"
"#define CLINET_SCRIPT_UNREGISTRATION {2}";

constexpr std::string_view generatedClientModuleDecl = "void GeneratedInitialize() override; void GeneratedShutdown() override;";
constexpr std::string_view generatedClientModuleImpl = "void {0}::GeneratedInitialize() {{ CLINET_SCRIPT_REGISTRATION CLINET_OBJECT_REGISTRATION }} void {0}::GeneratedShutdown() {{ CLINET_SCRIPT_UNREGISTRATION CLINET_OBJECT_UNREGISTRATION }}";

constexpr std::string_view scriptRegistration = "Engine::ScriptFactory::Register<{0}>();";
constexpr std::string_view scriptUnregistration = "Engine::ScriptFactory::Unregister<{0}>();";

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