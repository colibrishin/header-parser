#pragma once

constexpr auto scriptRegistrationBody =
"#pragma once\n"
"#include \"Script/Public/Script.h\"\n"
"{0}\n"
"#undef CLINET_SCRIPT_REGISTRATION\n"
"#undef CLINET_SCRIPT_UNREGISTRATION\n"
"#define CLINET_SCRIPT_REGISTRATION {1}\n"
"#define CLINET_SCRIPT_UNREGISTRATION {2}";

constexpr auto generatedClientModuleDecl = "void GeneratedInitialize() override; void GeneratedShutdown() override;";
constexpr auto generatedClientModuleImpl = "void {0}::GeneratedInitialize() {{ CLINET_SCRIPT_REGISTRATION }} void {0}::GeneratedShutdown() {{ CLINET_SCRIPT_UNREGISTRATION }}";

constexpr auto scriptRegistration = "Engine::ScriptFactory::Register<{0}>();";
constexpr auto scriptUnregistration = "Engine::ScriptFactory::Unregister<{0}>();";