#include "pages/files/files_view_model.hpp"

#include "test_fixture.hpp"

#include <lvgl.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace {

struct RevisionProbe {
    int calls          = 0;
    std::int32_t value = 0;
};

void revision_changed(lv_observer_t *observer, lv_subject_t *subject)
{
    auto *probe = static_cast<RevisionProbe *>(lv_observer_get_user_data(observer));
    assert(probe != nullptr);
    ++probe->calls;
    probe->value = lv_subject_get_int(subject);
}

std::size_t find_presentation(const FilesViewModel &view_model, std::string_view name)
{
    for (std::size_t index = 0; index < view_model.entry_count(); ++index) {
        const FilesEntryPresentation *entry = view_model.entry(index);
        if (entry != nullptr && entry->name == name) return index;
    }
    assert(false && "fixture presentation was not found");
    return FilesModel::maximum_entries;
}

void assert_subject_strings(const FilesViewModel &view_model)
{
    assert(std::strcmp(lv_subject_get_string(const_cast<FilesViewModel &>(view_model).query_subject()),
                       view_model.query().c_str()) == 0);
    assert(std::strcmp(lv_subject_get_string(const_cast<FilesViewModel &>(view_model).folder_name_subject()),
                       view_model.folder_name().c_str()) == 0);
    assert(std::strcmp(lv_subject_get_string(const_cast<FilesViewModel &>(view_model).display_path_subject()),
                       view_model.display_path().c_str()) == 0);
    assert(std::strcmp(lv_subject_get_string(const_cast<FilesViewModel &>(view_model).directory_name_subject()),
                       view_model.directory_name().c_str()) == 0);
    assert(std::strcmp(lv_subject_get_string(const_cast<FilesViewModel &>(view_model).error_subject()),
                       view_model.error_text().c_str()) == 0);
    assert(std::strcmp(lv_subject_get_string(const_cast<FilesViewModel &>(view_model).storage_summary_subject()),
                       view_model.storage().summary.c_str()) == 0);
}

void test_commands_presentations_and_subjects()
{
    TemporaryDirectory fixture("view-model");
    fixture.make_directory("Documents");
    fixture.write_file("Documents/inside.txt", "inside\n");
    fixture.write_file("device-note.txt", "observable preview\n");
    fixture.write_file("firmware.bin", "binary\n");

    FilesViewModel view_model(fixture.root().string());
    assert(view_model.initialized());
    assert(view_model.screen() == FilesScreen::Home);
    assert(view_model.content_screen() == FilesScreen::Home);
    assert(view_model.entry_count() == 3);
    assert(view_model.entry(view_model.entry_count()) == nullptr);
    assert(view_model.search_count() == view_model.entry_count());
    assert(view_model.storage().used_percent >= 0);
    assert(view_model.storage().used_percent <= 100);
    assert(!view_model.storage().summary.empty());

    assert(lv_subject_get_int(view_model.screen_subject()) == static_cast<std::int32_t>(FilesScreen::Home));
    assert(lv_subject_get_int(view_model.content_screen_subject()) == static_cast<std::int32_t>(FilesScreen::Home));
    assert(lv_subject_get_int(view_model.entry_count_subject()) == 3);
    assert(lv_subject_get_int(view_model.search_count_subject()) == 3);
    assert(lv_subject_get_int(view_model.supported_actions_subject()) == 0);
    assert(lv_subject_get_int(view_model.enabled_actions_subject()) == 0);
    assert(lv_subject_get_int(view_model.storage_percent_subject()) == view_model.storage().used_percent);
    assert_subject_strings(view_model);

    const FilesCommandResult no_preview = view_model.show(FilesScreen::Preview);
    assert(!no_preview.succeeded());
    assert(no_preview.status() == FilesCommandStatus::InvalidArgument);
    assert(view_model.screen() == FilesScreen::Home);
    assert(!view_model.error_text().empty());
    view_model.set_query({});
    assert(view_model.error_text().empty());

    RevisionProbe probe;
    lv_observer_t *observer = lv_subject_add_observer(view_model.content_revision_subject(), revision_changed, &probe);
    assert(observer != nullptr);
    const int observer_calls    = probe.calls;
    const std::int32_t revision = lv_subject_get_int(view_model.content_revision_subject());

    view_model.set_query("NOTE");
    assert(view_model.query() == "NOTE");
    assert(view_model.search_count() == 1);
    assert(view_model.search_entry(0) != nullptr);
    assert(view_model.search_entry(0)->name == "device-note.txt");
    assert(lv_subject_get_int(view_model.search_count_subject()) == 1);
    assert(std::strcmp(lv_subject_get_string(view_model.query_subject()), "NOTE") == 0);
    assert(probe.calls == observer_calls + 1);
    assert(probe.value == revision + 1);

    const std::string long_query(100, 'q');
    const std::string bounded_query(FilesModel::text_capacity - 1U, 'q');
    view_model.set_query(long_query);
    assert(view_model.query() == bounded_query);
    assert(std::strcmp(lv_subject_get_string(view_model.query_subject()), bounded_query.c_str()) == 0);
    view_model.set_query("NOTE");

    assert(view_model.show(FilesScreen::Search).succeeded());
    assert(view_model.screen() == FilesScreen::Search);
    assert(lv_subject_get_int(view_model.screen_subject()) == static_cast<std::int32_t>(FilesScreen::Search));
    assert(view_model.open_search_entry(0).succeeded());
    assert(view_model.screen() == FilesScreen::Preview);
    assert(view_model.preview_entry() != nullptr);
    assert(view_model.preview_entry()->name == "device-note.txt");
    assert(view_model.preview().find("observable preview") != std::string::npos);
    assert(std::strcmp(lv_subject_get_string(view_model.preview_subject()), view_model.preview().c_str()) == 0);
    assert(view_model.back().succeeded());
    assert(view_model.screen() == FilesScreen::Home);

    view_model.set_query({});
    const auto documents = find_presentation(view_model, "Documents");
    assert(view_model.open_entry(documents).succeeded());
    assert(view_model.screen() == FilesScreen::Folder);
    assert(view_model.content_screen() == FilesScreen::Folder);
    assert(view_model.display_path() == "Documents");
    assert(view_model.directory_name() == "Documents");
    assert(lv_subject_get_int(view_model.content_screen_subject()) == static_cast<std::int32_t>(FilesScreen::Folder));

    view_model.set_folder_name("Created-from-view-model");
    assert(view_model.create_folder().succeeded());
    assert(view_model.screen() == FilesScreen::Folder);
    assert(find_presentation(view_model, "Created-from-view-model") < view_model.entry_count());
    assert(lv_subject_get_int(view_model.entry_count_subject()) == static_cast<std::int32_t>(view_model.entry_count()));
    assert(view_model.folder_name() == FilesModel::default_folder_name);
    assert(std::strcmp(lv_subject_get_string(view_model.folder_name_subject()),
                       FilesModel::default_folder_name.data()) == 0);

    assert(view_model.show(FilesScreen::NewFolder).succeeded());
    view_model.set_folder_name("../invalid");
    const FilesCommandResult invalid_name = view_model.create_folder();
    assert(!invalid_name.succeeded());
    assert(invalid_name.status() == FilesCommandStatus::InvalidArgument);
    assert(view_model.screen() == FilesScreen::NewFolder);
    assert(view_model.folder_name() == "../invalid");
    assert(std::strcmp(lv_subject_get_string(view_model.folder_name_subject()), "../invalid") == 0);

    const std::string long_folder_name(100, 'f');
    const std::string bounded_folder_name(FilesModel::text_capacity - 1U, 'f');
    view_model.set_folder_name(long_folder_name);
    assert(view_model.folder_name() == bounded_folder_name);
    assert(view_model.error_text().empty());
    assert(std::strcmp(lv_subject_get_string(view_model.folder_name_subject()), bounded_folder_name.c_str()) == 0);
    assert(view_model.back().succeeded());
    assert(view_model.screen() == FilesScreen::Folder);
    assert(view_model.folder_name() == FilesModel::default_folder_name);
    assert(std::strcmp(lv_subject_get_string(view_model.folder_name_subject()),
                       FilesModel::default_folder_name.data()) == 0);

    assert(view_model.toggle_selection(0).succeeded());
    assert(view_model.selected_count() == 1);
    assert(lv_subject_get_int(view_model.selected_count_subject()) == 1);
    view_model.clear_selection();
    assert(view_model.selected_count() == 0);
    view_model.select_all();
    assert(view_model.selected_count() == view_model.entry_count());
    view_model.clear_selection();

    for (std::int32_t value = 0; value < static_cast<std::int32_t>(FilesAction::Count); ++value) {
        const auto capability = view_model.action_capability(static_cast<FilesAction>(value));
        assert(!capability.supported);
        assert(!capability.enabled);
        assert(!capability.reason.empty());
    }
    assert(view_model.supported_action_mask() == 0U);
    assert(view_model.enabled_action_mask() == 0U);

    const auto assert_unsupported = [&view_model](FilesCommandResult result) {
        assert(!result.succeeded());
        assert(result.status() == FilesCommandStatus::Unsupported);
        assert(!result.message().empty());
        assert(view_model.error_text() == result.message());
        assert(std::strcmp(lv_subject_get_string(view_model.error_subject()), result.message().c_str()) == 0);
    };
    assert_unsupported(view_model.copy_selection());
    assert_unsupported(view_model.rename_selection("renamed"));
    assert_unsupported(view_model.move_selection_to(0));
    assert_unsupported(view_model.archive_selection());
    assert_unsupported(view_model.remove_selection());
    assert_unsupported(view_model.change_sort());
    assert_unsupported(view_model.change_view());
    assert_unsupported(view_model.show(FilesScreen::Select));
    assert_unsupported(view_model.show(FilesScreen::Move));

    view_model.set_query("inside");
    assert(view_model.error_text().empty());
    assert(std::strlen(lv_subject_get_string(view_model.error_subject())) == 0U);
    const FilesCommandResult missing = view_model.open_entry(FilesModel::maximum_entries);
    assert(!missing.succeeded());
    assert(missing.status() == FilesCommandStatus::NotFound);
    assert(!view_model.error_text().empty());
    view_model.set_folder_name("valid-name");
    assert(view_model.error_text().empty());
    assert_subject_strings(view_model);

    assert(probe.calls > observer_calls + 10);
    assert(probe.value == lv_subject_get_int(view_model.content_revision_subject()));
    lv_observer_remove(observer);
}

}  // namespace

int main()
{
    lv_init();
    {
        test_commands_presentations_and_subjects();
    }
    lv_deinit();
    return 0;
}
