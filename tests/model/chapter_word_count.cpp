#include "reading/chapter.h"
#include "check.h"
#include <climits>
#include <cstdio>

using kobalt::ChapterTarget;
using kobalt::spineWordCount;
using Chapters = QVector<ChapterTarget>;

int main() {
    const Chapters spine{{"Front", "front", 0, 0}, {"One", "one", 0, 0}, {"Two", "two", 0, 0}};
    // Named chapter anchors split the file's prefix from the chapter text.
    // Counting only the fragment-free rows silently loses the whole story.
    const Chapters anchored{{"Front", "front", -1, 100},
                            {"One file", "one", -1, 0},
                            {"One", "one#heading", 1, 200},
                            {"Two file", "two", -1, 5},
                            {"Two", "two#heading", 1, 300}};
    require(spineWordCount(spine, anchored) == 605, "Named chapter anchors lost the story or its prefix");
    // A spine row followed by a TOC row at the file start covers the whole file.
    // The following TOC intervals cover it again; count that coverage only once.
    const Chapters overlap{{"Front", "front", -1, 100},
                           {"One file", "one", -1, 200},
                           {"One", "one", 1, 80},
                           {"Part", "one#part", 2, 120},
                           {"Two", "two", -1, 300}};
    require(spineWordCount(spine, overlap) == 600, "Whole-file and nested TOC counts overlapped");
    require(spineWordCount({{"One", "one", 0, 0}}, {{"File", "one", -1, 0},
                                                    {"Heading", "one#heading", 1, 0},
                                                    {"Heading alias", "one#heading", 1, 200}}) == 200,
            "Duplicate anchor intervals changed the total");
    require(spineWordCount({{"One", "one", 0, 0}, {"Again", "one", 0, 0}}, {{"One", "one", -1, 200}}) == 200,
            "A repeated spine target counted a resource twice");
    require(spineWordCount({{"Empty", "one", 0, 0}}, {{"Empty", "one", -1, 0}}) == 0,
            "An empty resource became an unavailable count");
    require(spineWordCount({{"One", "one", 0, 0}, {"Two", "two", 0, 0}},
                           {{"One", "one", -1, INT_MAX}, {"Two", "two", -1, INT_MAX}}) == qint64(INT_MAX) * 2,
            "The book total overflowed a chapter count");
    require(spineWordCount(spine, {{"Front", "front", -1, 100}}) == -1, "Missing files supplied a partial total");
    require(spineWordCount({{"One", "one", 0, 0}}, {{"Part", "one#part", 1, 200}}) == -1,
            "A missing prefix supplied a partial total");
    require(spineWordCount({{"Part", "one#part", 0, 0}}, {{"Part", "one#part", 1, 200}}) == -1,
            "A fragment-only spine fallback supplied a book total");
    require(spineWordCount({{"One", "one", 0, 0}}, {{"One", "one", -1, -1}}) == -1,
            "An unavailable interval supplied a total");
    require(spineWordCount({{"One", "one", 0, 0}}, {{"Other", "other", -1, 100}}) == -1,
            "An unrelated resource supplied a total");
    require(spineWordCount({{"One", "one", 0, 0}},
                           {{"One", "one", -1, 100}, {"Part", "one#part", 1, 50}, {"Back to start", "one", 1, 100}}) ==
                -1,
            "An interval returning to the file start supplied a total");
    require(spineWordCount({{"One", "one", 0, 0}, {"Two", "two", 0, 0}},
                           {{"One", "one", -1, 100}, {"Two", "two", -1, 100}, {"Again", "one", -1, 100}}) == -1,
            "A repeated resource group supplied an overlapping total");
    require(spineWordCount({}, {}) == -1, "An absent book supplied a count");
    std::puts("PASS: book counts cover named anchors, prefixes and nested intervals exactly once");
}
