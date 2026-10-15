#include "sql.hpp"
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    int offset = argc > 1 && std::string(argv[1]) == "sql" ? 1 : 0;
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::cout << "NoqeriSQL native adapter 1.0.0-candidate\n";
        return 0;
    }
    if (argc != 4 + offset) {
        std::cerr << "usage: noqerisql [sql] <sqlite|postgres> <database> <script.sql>\n";
        return 2;
    }
    return noe::executeNoqeriSql(argv[1 + offset], argv[2 + offset], argv[3 + offset],
                                 std::cout, std::cerr) ? 0 : 1;
}
