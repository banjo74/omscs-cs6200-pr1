#include "Graph.hpp"

#include "characters.hpp"

#include <algorithm>
#include <ranges>

namespace generator {
namespace {
std::set<char> validate_words(Words const& words) {
    std::set<char> startsWord;
    for (auto const& [text, info] : words) {
        if (text.empty()) {
            throw InvalidWord{text};
        }
        if (auto iter = std::ranges::find_if(
                text, [](auto const c) { return !is_word_character(c); });
            iter != text.end()) {
            throw InvalidWord{text};
        }
        if (is_digit(text[0])) {
            throw InvalidWord{text};
        }
        startsWord.insert(text[0]);
    }
    return startsWord;
}

void validate_starts_generic_word(std::set<char> const& startsGenericWord,
                                  std::set<char> const& startsWord) {
    for (auto const c : startsGenericWord) {
        if (!is_word_character(c) || is_digit(c) || startsWord.contains(c)) {
            throw InvalidStartsGenericWordCharacter{c};
        }
    }
}

void validate_terminator(std::string const& terminator) {
    if (auto iter = std::ranges::find_if(terminator, is_word_character);
        terminator.empty() || iter != terminator.end()) {
        throw InvalidTerminator{terminator};
    }
}

void validate_inputs(Words const&          words,
                     std::set<char> const& startsGenericWord,
                     std::string const&    terminator) {
    auto const startsWord = validate_words(words);
    validate_starts_generic_word(startsGenericWord, startsWord);
    validate_terminator(terminator);
}
} // namespace

Graph build_graph(Words const&          words,
                  std::set<char> const& startsGenericWord,
                  std::string const&    terminator) {
    validate_inputs(words, startsGenericWord, terminator);

    Graph graph{NumBaseStates};

    graph[Start][space()]         = Action{InSpace, true, {}};
    graph[InSpace][space()]       = Action{InSpace, true, {}};
    graph[InDigits][space()]      = Action{InSpace, false, {Number{}}};
    graph[InGenericWord][space()] = Action{InSpace, false, {GenericWord{}}};

    for (auto const d : digit_characters()) {
        // get into InDigits
        graph[Start][d]   = Action{InDigits, true, {}};
        graph[InSpace][d] = Action{InDigits, true, {}};
        // stay in InDigits
        graph[InDigits][d] = Action{InDigits, false, {}};
    }

    // get into InGenericWord
    for (auto const c : startsGenericWord) {
        graph[Start][c]   = Action{InGenericWord, true, {}};
        graph[InSpace][c] = Action{InGenericWord, true, {}};
    }

    // stay in InGenericWord
    for (auto const c : word_characters()) {
        graph[InGenericWord][c] = Action{InGenericWord, false, {}};
    }

    std::unordered_map<std::string, size_t> stemStates;
    auto const stemState = [&stemStates, &graph](std::string const& s) {
        auto [iter, success] = stemStates.emplace(s, 0);
        if (success) {
            iter->second = graph.size();
            graph.emplace_back();
        }
        return iter->second;
    };

    for (auto const& [text, info] : words) {
        std::string  stem{text[0]};
        size_t const startStemState = stemState(stem);
        // get into this word
        graph[Start][text[0]]   = Action{startStemState, true, {}};
        graph[InSpace][text[0]] = Action{startStemState, true, {}};
        size_t previousState    = startStemState;
        for (size_t i = 1; i < text.size(); ++i) {
            stem += text[i];
            size_t const thisStemState    = stemState(stem);
            graph[previousState][text[i]] = Action{thisStemState, true, {}};
            previousState                 = thisStemState;
        }
        graph[previousState][space()] = Action{InSpace, true, {info}};
    }

    {
        std::string  stem{terminator[0]};
        size_t const startStemState   = stemState(stem);
        graph[InSpace][terminator[0]] = Action{startStemState, true, {}};
        graph[InDigits][terminator[0]] =
            Action{startStemState, false, {Number{}}};
        graph[InGenericWord][terminator[0]] =
            Action{startStemState, false, {GenericWord{}}};
        for (auto const& [text, info] : words) {
            graph[stemStates.at(text)][terminator[0]] =
                Action{startStemState, true, {info}};
        }
        size_t previousStemState = startStemState;
        for (size_t i = 1; i < terminator.size() - 1; ++i) {
            stem += terminator[i];
            size_t const thisStemState = stemState(stem);
            graph[previousStemState][terminator[i]] =
                Action{thisStemState, true, {}};
            previousStemState = thisStemState;
        }
        graph[previousStemState][terminator.back()] =
            Action{Finished, true, {}};
    }

    graph[InSpace]['\0']       = Action{Finished, true, {}};
    graph[InDigits]['\0']      = Action{Finished, false, {Number{}}};
    graph[InGenericWord]['\0'] = Action{Finished, false, {GenericWord{}}};
    for (auto const& [text, info] : words) {
        graph[stemStates.at(text)]['\0'] = Action{Finished, true, {info}};
    }

    return graph;
}

std::string state_string(size_t const s) {
    static std::unordered_map<BaseStates, std::string> const baseStates{
        {Start, "Start"},
        {InSpace, "InSpace"},
        {InDigits, "InDigits"},
        {InGenericWord, "InGenericWord"},
        {Finished, "Finished"},
        {Invalid, "Invalid"},
    };
    if (s < NumBaseStates) {
        return baseStates.at(BaseStates{s});
    }
    return std::to_string(s);
}

InvalidWord::InvalidWord(std::string w)
    : BuildGraphInvalidArgument{"Invalid word: " + w} {}

InvalidStartsGenericWordCharacter::InvalidStartsGenericWordCharacter(char c)
    : BuildGraphInvalidArgument{"Invalid start word character " + c} {}

InvalidTerminator::InvalidTerminator(std::string w)
    : BuildGraphInvalidArgument{"Invalid terminator: " + w} {}
} // namespace generator
