#include <ATMERA/Engine.hpp>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

int wmain(int argc, wchar_t** argv) {
    if (argc != 6 || (std::wstring_view(argv[1]) != L"install" &&
        std::wstring_view(argv[1]) != L"update")) {
        std::wcerr << L"Usage: ATMERA.ModuleTool.exe <install|update> <project-root> <dll> <module-id> <version>\n";
        return 2;
    }
    try {
        // Package IDs and versions are ASCII by contract; reject non-ASCII before conversion.
        for (int i : {4, 5}) {
            for (const wchar_t* p = argv[i]; *p; ++p) {
                if (*p > 0x7f) {
                    std::wcerr << L"Module ID and version must be ASCII.\n";
                    return 2;
                }
            }
        }
        std::string id, version;
        for (const wchar_t* p = argv[4]; *p; ++p) id.push_back(static_cast<char>(*p));
        for (const wchar_t* p = argv[5]; *p; ++p) version.push_back(static_cast<char>(*p));
        atmera::Engine engine(argv[2]);
        std::string error;
        const bool installed = std::wstring_view(argv[1]) == L"install"
            ? engine.install_module_versioned(argv[3], id, {}, version, error)
            : engine.update_module_versioned(argv[3], id, version, error);
        if (!installed) {
            std::cerr << error << '\n';
            return 1;
        }
        std::cout << id << ' ' << version << " installed in project\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
