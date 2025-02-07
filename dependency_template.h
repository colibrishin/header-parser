#pragma once

constexpr auto moduleBodyGenerationDecl =
"private: static const std::vector<std::string> s_dependencies_; public: const std::vector<std::string>& GetDependencies() const { return s_dependencies_; } ";

constexpr auto moduleBodyGenerationDef =
"const std::vector<std::string> {0}::s_dependencies_ = {{ {1} }};\n";