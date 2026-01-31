
#include "write_table.hpp"

#include "BaseStates.hpp"

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

void write_table(std::ostream&          stream,
                 Config const&          config,
                 CompressedGraph const& g) {
    Action const invalid{.toState = Invalid};

    if (config.makeStatic) {
        stream << "static ";
    }
    stream << config.classMapType << " " << config.classMapName << "["
           << g.class_.size() << "] = {";
    bool first = false;
    for (auto const& c : all_characters()) {
        if (first) {
            stream << ", ";
        }
        first = true;
        stream << static_cast<int>(g.class_[c]);
    }
    stream << "};" << std::endl;
    if (config.makeStatic) {
        stream << "static ";
    }
    stream << config.tableType << " " << config.tableVariableName << "["
           << g.graph.size() << "][" << g.numClasses() << "] = {" << std::endl;

    first = false;
    for (auto const& state : g.graph) {
        if (first) {
            stream << "," << std::endl;
        }
        first = true;
        stream << '{';
        bool firstAction = false;
        for (uint8_t c = 0; c < g.numClasses(); ++c) {
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
