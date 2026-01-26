
#include "write_table.hpp"

#include <functional>
#include <iostream>

namespace generator {
namespace {
using Config = WriteTableConfig;

void write_bool(std::ostream& stream, bool const x) {
    stream << int{x};
}

void write_to_state(std::ostream& stream, Config const&, Action const& action) {
    stream << action.toState;
}

void write_record_reset(std::ostream& stream,
                        Config const&,
                        Action const& action) {
    write_bool(stream, action.resetRecording);
}

struct WriteTokenId {
    std::ostream& stream;
    Config const& config;

    void operator()(GenericWord const&) const {
        stream << config.genericWordId;
    }

    void operator()(Number const&) const {
        stream << config.numberId;
    }

    void operator()(WordInfo const& info) const {
        stream << info.id;
    }
};

void write_token_id(std::ostream& stream,
                    Config const& config,
                    Action const& action) {
    if (action.token) {
        std::visit(WriteTokenId{stream, config}, *action.token);
    } else {
        stream << config.noTokenId;
    }
}

void write_fields(std::ostream& stream,
                  Config const& config,
                  Action const& action) {
    using WriteFieldFcn =
        std::function<void(std::ostream&, Config const&, Action const&)>;
    static std::unordered_map<FieldNames, WriteFieldFcn> writers{
        {FieldNames::ToState, write_to_state},
        {FieldNames::RecordReset, write_record_reset},
        {FieldNames::TokenId, write_token_id},
    };

    bool first = false;
    stream << '{';
    for (auto const fieldName : config.fieldsToWrite) {
        if (first) {
            stream << ',' << std::endl;
        }
        first = true;
        writers.at(fieldName)(stream, config, action);
    }
    stream << '}';
}

} // namespace

void write_table(std::ostream& stream, Config const& config, Graph const& g) {
    Action const invalid{.toState = Invalid};

    if (config.makeStatic) {
        stream << "static ";
    }
    stream << config.tableType << " " << config.tableVariableName << "["
           << g.size() << "][128] = {" << std::endl;

    bool firstState = false;
    for (auto const& state : g) {
        if (firstState) {
            stream << "," << std::endl;
        }
        firstState = true;
        stream << '{';
        bool firstAction = false;
        for (size_t i = 0; i < 128; ++i) {
            char const c = static_cast<char>(i);
            if (firstAction) {
                stream << "," << std::endl;
            }
            firstAction = true;
            if (auto const iter = state.find(c); iter != state.end()) {
                write_fields(stream, config, iter->second);
            } else {
                write_fields(stream, config, invalid);
            }
        }
        stream << '}';
    }
    stream << '}' << ';';
}
} // namespace generator
