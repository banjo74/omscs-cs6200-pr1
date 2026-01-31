#include "Graph.hpp"
#include "build_graph.hpp"
#include "compress_graph.hpp"
#include "write_table.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#ifndef TEST_MODE
int main(int argc, char** argv) try {
    using namespace generator;
    if (argc < 2) {
        throw std::runtime_error{"too few inputs"};
    }
    if (argc > 2) {
        throw std::runtime_error{"too main inputs"};
    }
    std::filesystem::path const outputFile{argv[1]};

    auto const graph =
        compress_graph(build_graph({{"GETFILE", {"GetfileToken"}},
                                    {"GET", {"GetToken"}},
                                    {"OK", {"OkToken"}},
                                    {"FILE_NOT_FOUND", {"FileNotFoundToken"}},
                                    {"ERROR", {"ErrorToken"}},
                                    {"INVALID", {"InvalidToken"}}},
                                   {'/'},
                                   "\r\n\r\n"));

    std::ofstream stream{outputFile};
    if (!stream) {
        throw std::runtime_error{"cannot open: " + outputFile.string()};
    }

    write_table(stream,
                {.fieldsToWrite     = {FieldNames::ToState,
                                       FieldNames::RecordReset,
                                       FieldNames::TokenId},
                 .genericWordId     = "PathToken",
                 .numberId          = "SizeToken",
                 .noTokenId         = "UnknownToken",
                 .tableType         = "struct Action const",
                 .tableVariableName = "action_table_",
                 .classMapType      = "const uint8_t",
                 .classMapName      = "character_class_"},
                graph);

    stream.close();

    return 0;
} catch (std::exception const& exe) {

    std::cerr << argv[0] << ":" << exe.what() << std::endl;
    return 1;
}
#endif
