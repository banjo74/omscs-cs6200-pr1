#include "build_graph.hpp"

#include "BaseStates.hpp"
#include "Graph.hpp"
#include "characters.hpp"

namespace generator {
namespace {
// check that all words are non-empty and consist only of word
// characters and don't start with a number.  Returns the set of characters that
// start the words.  The starts generic word characters cannot be among these
// characters.
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

// make sure that none of the startsGenericWord characters appear in startsWord.
void validate_starts_generic_word(std::set<char> const& startsGenericWord,
                                  std::set<char> const& startsWord) {
    // don't bother with set_interesection, we're just going to report the first
    // one.
    for (auto const c : startsGenericWord) {
        if (!is_word_character(c) || is_digit(c) || startsWord.contains(c)) {
            throw InvalidStartsGenericWordCharacter{c};
        }
    }
}

// terminators must be non-word characters
void validate_terminator(std::string const& terminator) {
    if (auto iter = std::ranges::find_if(terminator, is_word_character);
        terminator.empty() || iter != terminator.end()) {
        throw InvalidTerminator{terminator};
    }
}

// validate all of the inputs
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

    // okay, stems.  this is a map from a stem to a state
    std::unordered_map<std::string, size_t> stemStates;

    // stemState(stem)
    // if stem is not in stemStates, add it and assign an index (a new state)
    // return the state index for the stem
    auto const stemState = [&stemStates, &graph](std::string const& s) {
        auto [iter, success] = stemStates.emplace(s, graph.size());
        if (success) {
            // new state
            graph.emplace_back();
        }
        return iter->second;
    };

    for (auto const& [text, info] : words) {
        // start the stem with the first character and get that stem's state
        std::string  stem{text[0]};
        size_t const startStemState = stemState(stem);
        // get into this word
        graph[Start][text[0]]   = Action{startStemState, true, {}};
        graph[InSpace][text[0]] = Action{startStemState, true, {}};

        // now for the rest
        size_t previousState = startStemState;
        for (size_t i = 1; i < text.size(); ++i) {
            stem += text[i];
            size_t const thisStemState = stemState(stem);
            // previous state X this character -> this state
            graph[previousState][text[i]] = Action{thisStemState, true, {}};
            previousState                 = thisStemState;
        }
        // get out of the word (and found a token)
        graph[previousState][space()] = Action{InSpace, true, {info}};
    }

    {
        // do basically the same thing for the terminator
        std::string  stem{terminator[0]};
        size_t const startStemState = stemState(stem);

        // except a token may appear immediately before the terminator so we
        // need to get from all of the token states to the terminator state
        graph[InSpace][terminator[0]] = Action{startStemState, true, {}};
        graph[InDigits][terminator[0]] =
            Action{startStemState, false, {Number{}}};
        graph[InGenericWord][terminator[0]] =
            Action{startStemState, false, {GenericWord{}}};
        for (auto const& [text, info] : words) {
            graph[stemStates.at(text)][terminator[0]] =
                Action{startStemState, true, {info}};
        }

        // just like words above but we don't process the last character...
        size_t previousStemState = startStemState;
        for (size_t i = 1; i < terminator.size() - 1; ++i) {
            stem += terminator[i];
            size_t const thisStemState = stemState(stem);
            graph[previousStemState][terminator[i]] =
                Action{thisStemState, true, {}};
            previousStemState = thisStemState;
        }
        // because that takes us directly to the finished state.
        graph[previousStemState][terminator.back()] =
            Action{Finished, true, {}};
    }
    // and so does null character
    graph[InSpace]['\0']       = Action{Finished, true, {}};
    graph[InDigits]['\0']      = Action{Finished, false, {Number{}}};
    graph[InGenericWord]['\0'] = Action{Finished, false, {GenericWord{}}};
    for (auto const& [text, info] : words) {
        graph[stemStates.at(text)]['\0'] = Action{Finished, true, {info}};
    }

    return graph;
}

InvalidWord::InvalidWord(std::string w)
    : BuildGraphInvalidArgument{"Invalid word: " + w} {}

InvalidStartsGenericWordCharacter::InvalidStartsGenericWordCharacter(char c)
    : BuildGraphInvalidArgument{"Invalid start word character " + c} {}

InvalidTerminator::InvalidTerminator(std::string w)
    : BuildGraphInvalidArgument{"Invalid terminator: " + w} {}
} // namespace generator
