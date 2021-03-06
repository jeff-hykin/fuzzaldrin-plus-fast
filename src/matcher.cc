#include <iterator>
#include <algorithm>

#include "common.h"

//----------------------------------------------------------------------
// Align sequence (used for fuzzaldrin.match)
// Return position of subject characters that match query.
//
// Follow closely scorer.computeScore.
// Except at each step we record what triggered the best score.
// Then we trace back to output matched characters.
//
// Differences are:
// - we record the best move at each position in a matrix, and finish by a traceback.
// - we reset consecutive sequence if we do not take the match.
// - no hit miss limit
std::vector<size_t> computeMatch(const CandidateString &subject, const CandidateString &subject_lw, const PreparedQuery &preparedQuery, size_t offset = 0) {
    const auto &query = preparedQuery.query;
    const auto &query_lw = preparedQuery.query_lw;

    const int m = subject.size();
    const int n = query.size();

    // this is like the consecutive bonus, but for camelCase / snake_case initials
    const auto acro = scoreAcronyms(subject, subject_lw, query, query_lw);
    const auto acro_score = acro.score;

    // Init
    vector<Score> score_row(n, 0);
    vector<Score> csc_row(n, 0);

    // Directions constants
    enum Direction { STOP,
        UP,
        LEFT,
        DIAGONAL };

    // Traceback matrix
    std::vector<Direction> trace(m * n, STOP);
    auto pos = -1;

    auto i = -1;
    while (++i < m) {//foreach char si of subject
        Score score = 0;
        Score score_up = 0;
        Score csc_diag = 0;
        const auto si_lw = subject_lw[i];

        auto j = -1;//0..n-1
        while (++j < n) {//foreach char qj of query
            // reset score
            Score csc_score = 0;
            Score align = 0;
            const auto score_diag = score_up;
            Direction move;

            //Compute a tentative match
            if (query_lw[j] == si_lw) {
                const auto start = isWordStart(i, subject, subject_lw);

                // Forward search for a sequence of consecutive char
                csc_score = csc_diag > 0 ? csc_diag : scoreConsecutives(subject, subject_lw, query, query_lw, i, j, start);

                // Determine bonus for matching A[i] with B[j]
                align = score_diag + scoreCharacter(i, j, start, acro_score, csc_score);
            }
            // Prepare next sequence & match score.
            score_up = score_row[j];// Current score_up is next run score diag
            csc_diag = csc_row[j];

            // In case of equality, moving UP get us closer to the start of the candidate string.
            if (score > score_up) {
                move = LEFT;
            } else {
                score = score_up;
                move = UP;
            }

            // Only take alignment if it's the absolute best option.
            if (align > score) {
                score = align;
                move = DIAGONAL;
            } else {
                // If we do not take this character, break consecutive sequence.
                // (when consecutive is 0, it'll be recomputed)
                csc_score = 0;
            }

            score_row[j] = score;
            csc_row[j] = csc_score;
            trace[++pos] = score > 0 ? move : STOP;
        }
    }

    // -------------------
    // Go back in the trace matrix
    // and collect matches (diagonals)

    i = m - 1;
    auto j = n - 1;
    pos = i * n + j;
    auto backtrack = true;
    std::vector<size_t> matches;

    while (backtrack && i >= 0 && j >= 0) {
        switch (trace[pos]) {
        case UP:
            i--;
            pos -= n;
            break;
        case LEFT:
            j--;
            pos--;
            break;
        case DIAGONAL:
            matches.push_back(i + offset);
            j--;
            i--;
            pos -= n + 1;
            break;
        default:
            backtrack = false;
        }
    }

    std::reverse(std::begin(matches), std::end(matches));
    return matches;
}

std::vector<size_t> basenameMatch(const CandidateString &subject, const CandidateString &subject_lw, const PreparedQuery &preparedQuery, char pathSeparator) {
    // Skip trailing slashes
    int end = subject.size() - 1;
    while (subject[end] == pathSeparator) {
        end--;
    }

    // Get position of basePath of subject.
    auto basePos = subject.rfind(pathSeparator, end);

    // If no PathSeparator, no base path exist.
    if (basePos == std::string::npos)
        return std::vector<size_t>();

    // Get the number of folder in query
    auto depth = preparedQuery.depth;

    // Get that many folder from subject
    while (depth-- > 0) {
        basePos = subject.rfind(pathSeparator, basePos - 1);
        if (basePos == std::string::npos)// consumed whole subject ?
            return std::vector<size_t>();
    }

    // Get basePath match
    basePos++;
    end++;
    return computeMatch(subject.substr(basePos, end - basePos),
      subject_lw.substr(basePos, end - basePos),
      preparedQuery,
      basePos);
}


//
// Combine two matches result and remove duplicate
// (Assume sequences are sorted, matches are sorted by construction.)
//
std::vector<size_t> mergeMatches(const std::vector<size_t> &a, const std::vector<size_t> &b) {
    const int m = a.size();
    const int n = b.size();

    if (n == 0) return a;
    if (m == 0) return b;

    auto i = -1;
    auto j = 0;
    auto bj = b[j];
    std::vector<size_t> out;

    while (++i < m) {
        auto ai = a[i];

        while (bj <= ai && ++j < n) {
            if (bj < ai)
                out.push_back(bj);
            bj = b[j];
        }
        out.push_back(ai);
    }
    while (j < n)
        out.push_back(b[j++]);
    return out;
}

// Return position of character which matches
std::vector<size_t> matcher_match(const CandidateString &string, const Element &query, const Options &options) {
    const auto string_lw = ToLower(string);
    auto matches = computeMatch(string, string_lw, options.preparedQuery);

    if (string.find(options.pathSeparator) != std::string::npos) {
        const auto baseMatches = basenameMatch(string, string_lw, options.preparedQuery, options.pathSeparator);
        return mergeMatches(matches, baseMatches);
    }
    return matches;
}

void get_wrap(const CandidateString &string, const Element &query, const Options &options, std::string *out) {
    const std::string tagClass = "highlight";
    const auto tagOpen = "<strong class=\"" + tagClass + "\">";
    const std::string tagClose = "</strong>";

    if (string == query) {
        *out = tagOpen + string + tagClose;
        return;
    }

    // Run get position where a match is found
    auto matchPositions = matcher_match(string, query, options);

    // If no match return as is
    if (matchPositions.size() == 0) {
        *out = string;
        return;
    }

    // Loop over match positions
    std::string output;
    size_t matchIndex = 0;
    size_t strPos = 0;
    while (matchIndex < matchPositions.size()) {
        auto matchPos = matchPositions[matchIndex];
        matchIndex++;

        // Get text before the current match position
        if (matchPos > strPos) {
            output += string.substr(strPos, matchPos - strPos);
            strPos = matchPos;
        }

        // Get consecutive matches to wrap under a single tag
        while (matchIndex < matchPositions.size()) {
            matchIndex++;
            if (matchPositions[matchIndex - 1] == matchPos + 1) {
                matchPos++;
            } else {
                matchIndex--;
                break;
            }
        }

        // Get text inside the match, including current character
        matchPos++;
        if (matchPos > strPos) {
            output += tagOpen;
            output += string.substr(strPos, matchPos - strPos);
            output += tagClose;
            strPos = matchPos;
        }
    }

    // Get string after last match
    if (strPos <= string.size() - 1) {
        output += string.substr(strPos);
    }

    // return wrapped text
    *out = output;
}
