#include "engine/crengine/reader_model.h"
#include "publication/publication.h"
#include "reader_inputs.h"
#include "reader_source_index_probe.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUrl>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sys/resource.h>
#ifdef __APPLE__
#include <mach/mach.h>
#endif

using namespace kobalt;
namespace {
using Ranges = QVector<QPair<QString, QString>>;
void require(bool condition, const QString &message) {
    if (!condition) {
        fprintf(stderr, "%s\n", qPrintable(message));
        std::exit(1);
    }
}
QByteArray read(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), file.errorString());
    return file.readAll();
}
void write(const QString &path, const QJsonObject &object) {
    QSaveFile file(path);
    require(file.open(QIODevice::WriteOnly), file.errorString());
    const auto bytes = QJsonDocument(object).toJson();
    require(file.write(bytes) == bytes.size() && file.commit(), file.errorString());
}
qint64 residentBytes() {
#ifdef __APPLE__
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
        return info.resident_size;
#endif
    return -1;
}
double cpuMilliseconds() {
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return 1000.0 * (usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) +
           (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000.0;
}
template <typename F> QJsonObject measure(int repetitions, F action) {
    QVector<double> samples;
    const auto cpu = cpuMilliseconds();
    for (int i = 0; i < repetitions; ++i) {
        QElapsedTimer timer;
        timer.start();
        action(i);
        samples.append(timer.nsecsElapsed() / 1000000.0);
    }
    const double elapsedCpu = cpuMilliseconds() - cpu;
    std::sort(samples.begin(), samples.end());
    double total = 0;
    for (const auto value : samples)
        total += value;
    return {{"repetitions", repetitions}, {"mean_ms", total / repetitions},
            {"median_ms", samples[samples.size() / 2]},
            {"p95_ms", samples[qMin(samples.size() - 1, qsizetype(samples.size() * .95))]},
            {"max_ms", samples.last()}, {"cpu_total_ms", elapsedCpu}};
}
QJsonArray subset(const QJsonArray &all, int count, bool spread) {
    QJsonArray result;
    for (int i = 0; i < count; ++i) {
        const int index = spread && count > 1 ? qint64(i) * (all.size() - 1) / (count - 1) : i;
        result.append(all[index]);
    }
    return result;
}
Ranges rangesOf(const QJsonArray &entries) {
    Ranges result;
    for (const auto value : entries) {
        const auto entry = value.toObject();
        result.append({entry.value("start").toString(), entry.value("end").toString()});
    }
    return result;
}
QStringList endpointsOf(const Ranges &ranges) {
    QStringList result;
    for (const auto &range : ranges)
        result << range.first << range.second;
    return result;
}
QJsonObject prepare(ReaderModel &model, const QString &book, const QByteArray &bytes) {
    QJsonArray candidates;
    // Search supplies canonical production anchors. Adjacent sentence ends make
    // disjoint passages; collection and validation are outside timed sections.
    for (int page = 0; page < model.pages(); ++page) {
        model.go(page);
        const auto edges = model.annotationPageEdges();
        if (edges.first.start.isEmpty())
            continue;
        auto start = edges.first.start;
        for (const auto &hit : model.searchPage(".", page).hits) {
            const auto range = model.textRange(start, hit.end);
            if (range.text.size() < 100)
                continue;
            if (range.start != start)
                continue;
            if (range.text.size() <= 1200) {
                candidates.append(QJsonObject{{"id", QString("engine-benchmark-%1").arg(candidates.size())},
                                              {"start", range.start}, {"end", range.end}, {"text", range.text},
                                              {"chapter", model.chapterProgress().title},
                                              {"created", "2026-01-01T00:00:00Z"}, {"color", "gray"},
                                              {"style", "shaded"}, {"source_page", page}});
            }
            start = hit.end;
        }
        if (page % 200 == 0)
            fprintf(stderr, "fixture page %d/%d: %lld passages\n", page, model.pages(), qint64(candidates.size()));
    }
    require(candidates.size() >= 2000, "Book supplied fewer than 2000 nonoverlapping sentence passages");
    Publication publication;
    QString error;
    require(publication.inspect(bytes, &error), error);
    const auto sparse = subset(candidates, 2000, true);
    const auto concentrated = subset(candidates, 2000, false);
    const auto validate = [&](const QJsonArray &entries) {
        const auto ranges = rangesOf(entries);
        QVector<int> order;
        const auto pages = model.pagesOf(endpointsOf(ranges), true, &order);
        for (int i = 0; i < ranges.size(); ++i) {
            require(pages[2 * i] >= 0 && pages[2 * i + 1] >= 0, "Unresolved fixture endpoint");
            require(model.textRange(ranges[i].first, ranges[i].second).text == entries[i].toObject()["text"].toString(),
                    "Fixture text changed during validation");
            require(order[2 * i] <= order[2 * i + 1] && (i == 0 || order[2 * i - 1] <= order[2 * i]),
                    "Fixture ranges overlap or are out of source order");
        }
    };
    validate(sparse);
    validate(concentrated);
    auto settings = model.state();
    settings.remove("position");
    settings.remove("page");
    settings["fingerprint"] = QString::fromLatin1(publication.fingerprint());
    return {{"version", 1}, {"book", QFileInfo(book).canonicalFilePath()},
            {"identity", QUrl::fromLocalFile(QFileInfo(book).canonicalFilePath()).toString()},
            {"sha256", QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
            {"fingerprint", QString::fromLatin1(publication.fingerprint())},
            {"viewport", QJsonObject{{"width", 800}, {"height", 1040}}}, {"settings", settings},
            {"pages", model.pages()}, {"candidate_count", candidates.size()},
            {"start_position", sparse[500].toObject()["start"]},
            {"highlights", sparse}, {"concentrated_highlights", concentrated}};
}
QJsonObject benchmark(ReaderModel &model, const QJsonObject &fixture) {
    QJsonArray results;
    qint64 sink = 0;
    const auto rssBefore = residentBytes();
    for (const auto &distribution : {QString("spread"), QString("concentrated")}) {
        const auto all = fixture[distribution == "spread" ? "highlights" : "concentrated_highlights"].toArray();
        for (const int count : {0, 100, 500, 1000, 2000}) {
            const auto entries = subset(all, count, distribution == "spread");
            const auto ranges = rangesOf(entries);
            const auto endpoints = endpointsOf(ranges);
            QJsonObject result{{"distribution", distribution}, {"count", count}};
            result["pages_of_first_at_count"] = measure(1, [&](int) {
                QVector<int> order;
                sink += model.pagesOf(endpoints, true, &order).size();
            });
            result["pages_of_with_order"] = measure(10, [&](int) {
                QVector<int> order;
                sink += model.pagesOf(endpoints, true, &order).size();
            });
            result["pages_of_without_order"] = measure(10, [&](int) {
                sink += model.pagesOf(endpoints, true).size();
            });
            const int marked = (entries.isEmpty() ? all[all.size() / 2] : entries[entries.size() / 2])
                                   .toObject()["source_page"].toInt();
            for (const int target : {0, marked}) {
                model.go(target);
                const QString key = target == 0 ? "unmarked" : "marked";
                result[key + "_page"] = target;
                result[key + "_visible_count"] = model.visibleRanges(ranges).size();
                result[key + "_visible_ranges"] = measure(30, [&](int) { sink += model.visibleRanges(ranges).size(); });
                result[key + "_page_range_rects"] = measure(20, [&](int) {
                    const auto groups = model.pageRangeRects(target, ranges);
                    for (const auto &group : groups)
                        sink += group.size();
                });
                // ReaderSession::rangeRectGroups calls rangeRects once per input.
                // This isolates the engine work, excluding its worker queue/copy.
                result[key + "_worst_case_all_range_rects"] = measure(10, [&](int) {
                    for (const auto &range : ranges)
                        sink += model.rangeRects(range.first, range.second).size();
                });
                if (!ranges.isEmpty())
                    result[key + "_intersecting_ranges"] = measure(20, [&](int) {
                        sink += model.intersectingRanges(ranges.first().first, ranges.first().second, ranges).size();
                    });
            }
            // Different adjacent blocks keep the second case from reusing all
            // the first case's images. These are descriptive page-turn samples;
            // isolated filter measurements provide the controlled comparison.
            const int first = qMin(marked, model.pages() - 42);
            for (const bool markup : {false, true}) {
                const int from = first + (markup ? 20 : 0);
                result[markup ? "twenty_turns_with_markup" : "twenty_turns_without_markup"] = measure(20, [&](int i) {
                    const int page = from + i;
                    model.go(page);
                    sink += model.image(page).width();
                    if (markup) {
                        const auto groups = model.pageRangeRects(page, ranges);
                        for (const auto &group : groups)
                            sink += group.size();
                    }
                });
            }
            result["resident_bytes"] = residentBytes();
            results.append(result);
            fprintf(stderr, "%s %d done\n", qPrintable(distribution), count);
        }
    }
    return {{"results", results}, {"pages", model.pages()}, {"fixture_sha256", fixture["sha256"]},
            {"fingerprint", fixture["fingerprint"]}, {"settings", fixture["settings"]},
            {"viewport", fixture["viewport"]}, {"resident_before_bytes", rssBefore},
            {"resident_after_bytes", residentBytes()}, {"sink", sink},
            {"scope", "Native synchronous ReaderModel, warm document, no storage or GUI. Page turns use different adjacent page blocks; compare isolated filter costs for controlled markup overhead. RSS includes book bytes and both complete fixture arrays at every count, so it is not annotation-only memory. Worst-case all-range rectangles intentionally bypasses normal visible-range filtering."}};
}
QJsonObject benchmarkIndex(ReaderModel &model, const QJsonObject &fixture) {
    using Probe = ReaderSourceIndexProbe;
    QJsonArray results;
    qint64 sink = 0;
    for (const auto &distribution : {QString("spread"), QString("concentrated")}) {
        const auto all = fixture[distribution == "spread" ? "highlights" : "concentrated_highlights"].toArray();
        for (const int count : {0, 100, 500, 1000, 2000}) {
            const auto entries = subset(all, count, distribution == "spread");
            const auto ranges = rangesOf(entries);
            const auto anchors = endpointsOf(ranges);
            const int target = entries.isEmpty() ? 100 : entries[entries.size() / 2].toObject()["source_page"].toInt();
            model.go(target);
            QJsonObject result{{"distribution", distribution}, {"count", count}};
            result["source_index_reset_and_build"] = measure(3, [&](int) {
                Probe::clear(model);
                sink += model.visibleRanges(ranges).size();
            });
            result["source_index_warm_query"] = measure(100, [&](int) { sink += model.visibleRanges(ranges).size(); });
            result["original_source_scan"] = measure(10, [&](int) { sink += Probe::visible(model, ranges).size(); });
            const auto selection = ranges.isEmpty() ? QPair<QString, QString>() : ranges[ranges.size() / 2];
            result["indexed_overlap"] = measure(100, [&](int) {
                sink += model.intersectingRanges(selection.first, selection.second, ranges).size();
            });
            result["original_overlap_scan"] = measure(10, [&](int) {
                sink += Probe::overlaps(model, selection.first, selection.second, ranges).size();
            });
            auto others = ranges;
            if (!others.isEmpty())
                others.removeAt(others.size() / 2);
            result["overlap_after_excluding_one"] = measure(1, [&](int) {
                sink += model.intersectingRanges(selection.first, selection.second, others).size();
            });
            const Ranges drafts{selection, {selection.first, Probe::shifted(model, selection.second, -1)}};
            QVector<Ranges> combined;
            for (const auto &draft : drafts) {
                auto withDraft = ranges;
                withDraft.append(draft);
                combined.append(std::move(withDraft));
            }
            result["changing_draft_combined"] = measure(40, [&](int i) {
                sink += model.visibleRanges(combined[i % 2]).size();
            });
            model.visibleRanges(ranges); // Rebuild the saved-only snapshot outside the timed lane.
            result["changing_draft_separate"] = measure(40, [&](int i) {
                const auto &draft = drafts[i % 2];
                sink += model.visibleRanges(ranges).size() + model.rangeVisible(draft.first, draft.second);
            });
            result["indexed_geometry"] = measure(30, [&](int) {
                for (const auto &group : model.pageRangeRects(target, ranges))
                    sink += group.size();
            });
            result["source_index_accounted_bytes"] = Probe::usage(model).second;
            result["source_index_anchors"] = Probe::usage(model).first;
            Probe::clear(model);
            QVector<int> mappedPages, ranks;
            result["endpoint_cache_cold"] = measure(1, [&](int) { mappedPages = model.pagesOf(anchors, true, &ranks); });
            result["endpoint_cache_warm"] = measure(20, [&](int) { sink += model.pagesOf(anchors, true, &ranks).size(); });
            result["page_interval_linear_query"] = measure(100, [&](int) {
                for (int i = 0; i < ranges.size(); ++i)
                    sink += mappedPages[2 * i] >= 0 && mappedPages[2 * i] <= target && mappedPages[2 * i + 1] >= target;
            });
            auto added = anchors;
            added.append(model.pageStart(qMin(1234, model.pages() - 1)));
            result["endpoint_cache_add_one_anchor"] = measure(1, [&](int) { sink += model.pagesOf(added, true, &ranks).size(); });
            int sourceMismatches = 0, intervalMismatches = 0, overlapMismatches = 0;
            for (int sample = 0; sample <= 64; ++sample) {
                const int page = sample * (model.pages() - 1) / 64;
                model.go(page);
                const auto expected = Probe::visible(model, ranges);
                sourceMismatches += model.visibleRanges(ranges) != expected;
                QVector<int> interval;
                for (int i = 0; i < ranges.size(); ++i)
                    if (mappedPages[2 * i] >= 0 && mappedPages[2 * i] <= page && mappedPages[2 * i + 1] >= page)
                        interval.append(i);
                intervalMismatches += interval != expected;
                if (!ranges.isEmpty()) {
                    const auto &query = ranges[sample * (ranges.size() - 1) / 64];
                    overlapMismatches += model.intersectingRanges(query.first, query.second, ranges) !=
                                         Probe::overlaps(model, query.first, query.second, ranges);
                }
            }
            require(sourceMismatches == 0, "Indexed result differs from original source scan");
            require(overlapMismatches == 0, "Indexed overlap differs from original strict source scan");
            result["reference_pages_checked"] = 65;
            result["source_index_mismatches"] = sourceMismatches;
            result["overlap_mismatches"] = overlapMismatches;
            result["page_interval_mismatches"] = intervalMismatches;
            result["resident_bytes"] = residentBytes();
            results.append(result);
            fprintf(stderr, "index %s %d passed\n", qPrintable(distribution), count);
        }
    }
    return {{"results", results}, {"pages", model.pages()}, {"sink", sink}, {"sha256", fixture["sha256"]},
            {"scope", "Production source-position cache and interval tree against independent original algorithm. Cold source build includes reset; endpoint cold includes all geometry. Accounted bytes conservatively include pointer/key/hash storage and range/tree arrays, not process RSS delta. Page-interval candidate is a linear integer scan, built through pagesOf, not production code."}};
}
}
int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "minimal");
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    const auto arguments = app.arguments();
    require(arguments.size() >= 4, "Usage: --prepare BOOK FIXTURE | --model BOOK FIXTURE OUTPUT | --index BOOK FIXTURE OUTPUT");
    const bool preparing = arguments[1] == "--prepare";
    require(preparing || ((arguments[1] == "--model" || arguments[1] == "--index") && arguments.size() == 5), "Invalid arguments");
    const auto bytes = read(arguments[2]);
    QJsonObject fixture;
    if (!preparing) {
        fixture = QJsonDocument::fromJson(read(arguments[3])).object();
        require(fixture["sha256"].toString().toLatin1() == QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex(),
                "Fixture belongs to another book revision");
    }
    const QJsonObject settings = preparing
        ? QJsonObject{{"font_face", "Libron"}, {"font_pixels", 28}, {"line_spacing", 150},
                      {"line_spacing_version", 1}, {"margin_pixels", 32}, {"top_margin_pixels", 24},
                      {"bottom_margin_pixels", 24}, {"text_alignment", "justify"}, {"columns", 1}}
        : fixture["settings"].toObject();
    ReaderModel model(kobalt::test::readerFonts());
    QString error;
    QElapsedTimer opening;
    opening.start();
    require(model.open(bytes, QSize(800, 1040), settings, &error), error);
    fprintf(stderr, "Opened %d pages in %.3f ms, resident %lld bytes\n", model.pages(), opening.nsecsElapsed() / 1000000.0, residentBytes());
    if (preparing) {
        write(arguments[3], prepare(model, arguments[2], bytes));
    } else {
        require(model.pages() == fixture["pages"].toInt(), "Fixture pagination changed");
        auto result = arguments[1] == "--index" ? benchmarkIndex(model, fixture) : benchmark(model, fixture);
        write(arguments[4], result);
    }
}
