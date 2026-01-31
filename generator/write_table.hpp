#ifndef generator_write_table_hpp
#define generator_write_table_hpp

#include "CompressedGraph.hpp"

namespace generator {
enum class FieldNames {
    ToState,
    RecordReset,
    TokenId,
};

struct WriteTableConfig {
    std::vector<FieldNames> fieldsToWrite{FieldNames::ToState};

    std::string genericWordId = "GenericWord";
    std::string numberId      = "Number";
    std::string noTokenId     = "NotAToken";

    std::string tableType         = "Action";
    std::string tableVariableName = "table";

    std::string classMapType = "uint8_t";
    std::string classMapName = "class_";

    bool makeStatic = true;
};

void write_table(std::ostream& stream,
                 WriteTableConfig const&,
                 CompressedGraph const&);
} // namespace generator

#endif // include guard
