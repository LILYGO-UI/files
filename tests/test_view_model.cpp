#include "pages/files/files_view_model.hpp"

#include "test_fixture.hpp"

#include <lvgl.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <string>
#include <string_view>

#include <sys/time.h>

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

    assert(lv_subject_get_int(view_model.screen_subject()) == static_cast<std::int32_t>(FilesScreen::Home));
    assert(lv_subject_get_int(view_model.content_screen_subject()) == static_cast<std::int32_t>(FilesScreen::Home));
    assert(lv_subject_get_int(view_model.entry_count_subject()) == 3);
    assert(lv_subject_get_int(view_model.search_count_subject()) == 3);
    assert(lv_subject_get_int(view_model.supported_actions_subject()) ==
           static_cast<std::int32_t>(view_model.supported_action_mask()));
    assert(lv_subject_get_int(view_model.enabled_actions_subject()) ==
           static_cast<std::int32_t>(view_model.enabled_action_mask()));
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

    // Every action is supported. Sorting and view switching are enabled at once; item commands need a
    // long-pressed item and Paste needs a copied one.
    for (std::int32_t value = 0; value < static_cast<std::int32_t>(FilesAction::Count); ++value) {
        const auto action     = static_cast<FilesAction>(value);
        const auto capability = view_model.action_capability(action);
        const bool enabled    = action == FilesAction::ChangeSort || action == FilesAction::ChangeView;
        assert(capability.supported);
        assert(capability.enabled == enabled);
        assert(capability.reason.empty() == enabled);
    }
    assert(!view_model.action_capability(FilesAction::Count).supported);
    const std::uint32_t all_actions  = (1U << static_cast<std::uint32_t>(FilesAction::Count)) - 1U;
    const std::uint32_t enabled_mask = (1U << static_cast<std::uint32_t>(FilesAction::ChangeSort)) |
                                       (1U << static_cast<std::uint32_t>(FilesAction::ChangeView));
    assert(view_model.supported_action_mask() == all_actions);
    assert(view_model.enabled_action_mask() == enabled_mask);
    assert(lv_subject_get_int(view_model.supported_actions_subject()) == static_cast<std::int32_t>(all_actions));
    assert(lv_subject_get_int(view_model.enabled_actions_subject()) == static_cast<std::int32_t>(enabled_mask));

    // Without a long-pressed item, item commands fail visibly and change nothing.
    const auto assert_failed = [&view_model](FilesCommandResult result, FilesCommandStatus status) {
        assert(!result.succeeded());
        assert(result.status() == status);
        assert(!result.message().empty());
        assert(view_model.error_text() == result.message());
        assert(std::strcmp(lv_subject_get_string(view_model.error_subject()), result.message().c_str()) == 0);
        assert(view_model.screen() == FilesScreen::Folder);
    };
    const std::size_t entries_before = view_model.entry_count();
    assert_failed(view_model.copy_action_items(), FilesCommandStatus::NotFound);
    assert_failed(view_model.rename_action_entry(), FilesCommandStatus::NotFound);
    assert_failed(view_model.compress_action_items(), FilesCommandStatus::NotFound);
    assert_failed(view_model.duplicate_action_items(), FilesCommandStatus::NotFound);
    assert_failed(view_model.new_folder_with_action_items(), FilesCommandStatus::NotFound);
    assert_failed(view_model.remove_action_items(), FilesCommandStatus::NotFound);
    assert_failed(view_model.move_action_items(), FilesCommandStatus::NotFound);
    assert_failed(view_model.open_action_entry(), FilesCommandStatus::NotFound);
    assert_failed(view_model.show(FilesScreen::Move), FilesCommandStatus::NotFound);
    assert_failed(view_model.show(FilesScreen::Info), FilesCommandStatus::NotFound);
    assert_failed(view_model.paste(), FilesCommandStatus::InvalidArgument);
    assert_failed(view_model.show_actions(FilesModel::maximum_entries), FilesCommandStatus::NotFound);
    assert(view_model.entry_count() == entries_before);

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

void test_sort_view_and_selection_commands()
{
    TemporaryDirectory fixture("sort-view");
    fixture.make_directory("Folder");
    fixture.write_file("small.txt", "s\n");
    fixture.write_file("large.txt", std::string(4096, 'l'));

    FilesViewModel view_model(fixture.root().string());
    assert(view_model.sort_key() == FilesSortKey::Name);
    assert(view_model.sort_ascending());
    assert(view_model.view_mode() == FilesViewMode::Icons);
    assert(lv_subject_get_int(view_model.sort_key_subject()) == static_cast<std::int32_t>(FilesSortKey::Name));
    assert(lv_subject_get_int(view_model.sort_ascending_subject()) == 1);
    assert(lv_subject_get_int(view_model.view_mode_subject()) == static_cast<std::int32_t>(FilesViewMode::Icons));

    // Choosing from the more menu applies the choice and closes the menu.
    assert(view_model.show(FilesScreen::More).succeeded());
    assert(view_model.set_sort(FilesSortKey::Size).succeeded());
    assert(view_model.screen() == FilesScreen::Home);
    assert(view_model.sort_key() == FilesSortKey::Size);
    assert(!view_model.sort_ascending());
    assert(view_model.entry(1)->name == "large.txt");
    assert(lv_subject_get_int(view_model.sort_key_subject()) == static_cast<std::int32_t>(FilesSortKey::Size));
    assert(lv_subject_get_int(view_model.sort_ascending_subject()) == 0);

    assert(view_model.show(FilesScreen::More).succeeded());
    assert(view_model.set_view_mode(FilesViewMode::List).succeeded());
    assert(view_model.screen() == FilesScreen::Home);
    assert(view_model.view_mode() == FilesViewMode::List);
    assert(lv_subject_get_int(view_model.view_mode_subject()) == static_cast<std::int32_t>(FilesViewMode::List));

    // Selection mode toggles items instead of opening them and publishes the count.
    assert(view_model.show(FilesScreen::Select).succeeded());
    assert(view_model.selection_title() == "Select Items");
    assert(!view_model.all_selected());
    assert(view_model.activate_entry(0).succeeded());
    assert(view_model.screen() == FilesScreen::Select);
    assert(view_model.is_selected(0));
    assert(view_model.selection_title() == "1 Selected");
    assert(lv_subject_get_int(view_model.selected_count_subject()) == 1);
    view_model.toggle_select_all();
    assert(view_model.all_selected());
    assert(view_model.selection_title() == "3 Selected");
    view_model.toggle_select_all();
    assert(view_model.selected_count() == 0U);
    assert(view_model.activate_entry(2).succeeded());
    assert(view_model.back().succeeded());
    assert(view_model.screen() == FilesScreen::Home);
    assert(view_model.selected_count() == 0U);
    assert(lv_subject_get_int(view_model.selected_count_subject()) == 0);

    assert(view_model.activate_entry(0).succeeded());
    assert(view_model.screen() == FilesScreen::Folder);
}

void set_local_modified_time(const std::filesystem::path &path, int year, int month, int day)
{
    std::tm local{};
    local.tm_year             = year - 1900;
    local.tm_mon              = month - 1;
    local.tm_mday             = day;
    local.tm_hour             = 12;
    local.tm_isdst            = -1;
    const std::time_t seconds = std::mktime(&local);
    assert(seconds != static_cast<std::time_t>(-1));
    const timeval times[2] = {{seconds, 0}, {seconds, 0}};
    assert(::utimes(path.c_str(), times) == 0);
}

void test_grid_presentations()
{
    TemporaryDirectory fixture("presentation");
    fixture.make_directory("Empty");
    fixture.make_directory("Single");
    fixture.write_file("Single/only.txt", "one\n");
    fixture.make_directory("Pair");
    fixture.write_file("Pair/first.txt", "1\n");
    fixture.write_file("Pair/second.txt", "2\n");
    fixture.write_file("datasheet.PDF", std::string(2048, 'p'));
    fixture.write_file("notes.md", "notes\n");
    fixture.write_file("archive.tar.gz", "gz\n");
    fixture.write_file("Makefile", "all:\n");
    fixture.write_file("payload.longext", "x\n");
    set_local_modified_time(fixture.path("notes.md"), 2026, 9, 29);

    FilesViewModel view_model(fixture.root().string());
    assert(view_model.initialized());
    const auto entry = [&view_model](std::string_view name) {
        return view_model.entry(find_presentation(view_model, name));
    };

    // Folders show how many items they contain instead of a date and size.
    assert(entry("Empty")->detail == "0 items");
    assert(entry("Single")->detail == "1 item");
    assert(entry("Pair")->detail == "2 items");
    assert(!entry("Pair")->modified.empty());
    assert(entry("Pair")->summary == entry("Pair")->modified + " - 2 items");
    assert(entry("Pair")->type_label.empty());

    // Files show the local modification date, size and a short uppercase type label.
    assert(entry("notes.md")->modified == "2026/9/29");
    assert(entry("notes.md")->detail == "6 B");
    assert(entry("notes.md")->summary == "2026/9/29 - 6 B");
    assert(entry("notes.md")->type_label == "MD");
    assert(entry("datasheet.PDF")->type_label == "PDF");
    assert(entry("datasheet.PDF")->detail == "2.0 KB");
    assert(entry("archive.tar.gz")->type_label == "GZ");
    assert(entry("Makefile")->type_label.empty());
    assert(entry("payload.longext")->type_label.empty());
    assert(!entry("Makefile")->modified.empty());
}

void test_item_action_commands()
{
    TemporaryDirectory fixture("item-actions");
    fixture.write_file("Archive/old.txt", "old\n");
    fixture.write_file("notes.txt", "notes\n");
    set_local_modified_time(fixture.path("notes.txt"), 2026, 5, 4);

    FilesViewModel view_model(fixture.root().string());
    const auto open_actions = [&view_model](std::string_view name) {
        const std::size_t index = find_presentation(view_model, name);
        assert(view_model.show_actions(index).succeeded());
        assert(view_model.screen() == FilesScreen::Actions);
        assert(lv_subject_get_int(view_model.screen_subject()) == static_cast<std::int32_t>(FilesScreen::Actions));
        assert(view_model.action_index() == index);
        assert(view_model.action_entry() != nullptr && view_model.action_entry()->name == name);
    };

    // Get Info describes the item and opens it.
    open_actions("notes.txt");
    assert(view_model.action_capability(FilesAction::GetInfo).enabled);
    assert(view_model.show(FilesScreen::Info).succeeded());
    const FilesInfoPresentation *info = view_model.info();
    assert(info != nullptr);
    assert(info->name == "notes.txt");
    assert(info->kind == "Plain Text");
    assert(info->type_label == "TXT");
    assert(info->size.rfind("6 bytes (", 0) == 0);
    assert(info->size.size() > 9U && info->size.compare(info->size.size() - 9U, 9U, " on disk)") == 0);
    assert(info->modified == "2026/5/4 12:00");
    assert(!info->created.empty());
    assert(info->location == "Files");
    assert(info->can_open);
    assert(view_model.open_action_entry().succeeded());
    assert(view_model.screen() == FilesScreen::Preview);
    assert(view_model.preview_entry() != nullptr && view_model.preview_entry()->name == "notes.txt");
    assert(view_model.info() == nullptr);
    assert(view_model.back().succeeded());

    // Folders count their visible contents.
    open_actions("Archive");
    assert(view_model.show(FilesScreen::Info).succeeded());
    assert(view_model.info()->kind == "Folder");
    assert(view_model.info()->size.rfind("4 bytes (", 0) == 0);
    assert(view_model.info()->size.size() > 8U &&
           view_model.info()->size.compare(view_model.info()->size.size() - 8U, 8U, ", 1 item") == 0);
    assert(view_model.back().succeeded());
    assert(view_model.screen() == FilesScreen::Home);
    assert(view_model.action_entry() == nullptr);

    // Rename keeps the draft and explains a taken name.
    open_actions("notes.txt");
    assert(view_model.show(FilesScreen::Rename).succeeded());
    assert(view_model.rename_name() == "notes.txt");
    assert(std::strcmp(lv_subject_get_string(view_model.rename_name_subject()), "notes.txt") == 0);
    view_model.set_rename_name("Archive");
    const FilesCommandResult taken = view_model.rename_action_entry();
    assert(!taken.succeeded());
    assert(view_model.screen() == FilesScreen::Rename);
    assert(view_model.error_text().find("already taken") != std::string::npos);
    assert(view_model.rename_name() == "Archive");
    view_model.set_rename_name("journal.txt");
    assert(view_model.error_text().empty());
    assert(view_model.rename_action_entry().succeeded());
    assert(view_model.screen() == FilesScreen::Home);
    assert(view_model.rename_name().empty());
    find_presentation(view_model, "journal.txt");

    // Copy fills the clipboard and Paste adds a numbered copy.
    open_actions("journal.txt");
    assert(view_model.copy_action_items().succeeded());
    assert(view_model.screen() == FilesScreen::Home);
    assert(view_model.has_clipboard());
    assert(view_model.action_capability(FilesAction::Paste).enabled);
    assert(view_model.show(FilesScreen::More).succeeded());
    assert(view_model.paste().succeeded());
    assert(view_model.screen() == FilesScreen::Home);
    find_presentation(view_model, "journal 2.txt");

    open_actions("journal.txt");
    assert(view_model.duplicate_action_items().succeeded());
    find_presentation(view_model, "journal 3.txt");

    open_actions("Archive");
    assert(view_model.compress_action_items().succeeded());
    assert(find_presentation(view_model, "Archive.zip") < view_model.entry_count());
    assert(view_model.entry(find_presentation(view_model, "Archive.zip"))->type_label == "ZIP");

    // The Move picker browses folders; Move is available only away from the item's own folder.
    open_actions("journal 3.txt");
    assert(view_model.show(FilesScreen::Move).succeeded());
    assert(view_model.move_title() == "Files");
    assert(view_model.move_display_path() == "~/");
    assert(view_model.move_at_root());
    assert(!view_model.can_move_here());
    assert(view_model.move_destination_count() == 1U);
    assert(view_model.move_destination(0)->name == "Archive");
    assert(view_model.move_destination_enabled(0));
    assert(view_model.open_move_destination(0).succeeded());
    assert(view_model.move_title() == "Archive");
    assert(view_model.move_display_path() == "Archive");
    assert(view_model.move_parent_name() == "Files");
    assert(!view_model.move_at_root());
    assert(view_model.can_move_here());
    assert(view_model.move_destination_up().succeeded());
    assert(view_model.move_at_root());
    assert(view_model.open_move_destination(0).succeeded());
    assert(view_model.move_action_items().succeeded());
    assert(view_model.screen() == FilesScreen::Home);
    assert(std::filesystem::is_regular_file(fixture.path("Archive/journal 3.txt")));

    // A folder cannot be moved into itself.
    open_actions("Archive");
    assert(view_model.show(FilesScreen::Move).succeeded());
    assert(view_model.move_destination(0)->name == "Archive");
    assert(!view_model.move_destination_enabled(0));
    const FilesCommandResult into_itself = view_model.open_move_destination(0);
    assert(!into_itself.succeeded());
    assert(into_itself.status() == FilesCommandStatus::InvalidArgument);
    assert(view_model.screen() == FilesScreen::Move);
    assert(view_model.back().succeeded());

    // New Folder with 1 Item moves the item and offers to rename the new folder.
    open_actions("journal 2.txt");
    assert(view_model.new_folder_with_action_items().succeeded());
    assert(view_model.screen() == FilesScreen::Rename);
    assert(view_model.rename_name() == FilesModel::default_folder_name);
    assert(view_model.action_entry() != nullptr && view_model.action_entry()->is_directory);
    view_model.set_rename_name("Bundle");
    assert(view_model.rename_action_entry().succeeded());
    assert(std::filesystem::is_regular_file(fixture.path("Bundle/journal 2.txt")));

    open_actions("Bundle");
    assert(view_model.show(FilesScreen::ConfirmDelete).succeeded());
    assert(lv_subject_get_int(view_model.screen_subject()) == static_cast<std::int32_t>(FilesScreen::ConfirmDelete));
    assert(view_model.remove_action_items().succeeded());
    assert(view_model.screen() == FilesScreen::Home);
    assert(!std::filesystem::exists(fixture.path("Bundle")));

    // Item actions are not offered while selecting.
    assert(view_model.show(FilesScreen::Select).succeeded());
    assert(!view_model.show_actions(0).succeeded());
    assert(view_model.screen() == FilesScreen::Select);
}

void test_selection_action_commands()
{
    TemporaryDirectory fixture("selection-actions");
    fixture.make_directory("Inbox");
    fixture.write_file("first.txt", "1\n");
    fixture.write_file("second.txt", "2\n");

    FilesViewModel view_model(fixture.root().string());
    const auto select = [&view_model](std::initializer_list<std::string_view> names) {
        assert(view_model.show(FilesScreen::Select).succeeded());
        for (std::string_view name : names) {
            assert(view_model.toggle_selection(find_presentation(view_model, name)).succeeded());
        }
    };

    // A toolbar command without a selection fails visibly and keeps selection mode.
    select({});
    const FilesCommandResult nothing = view_model.duplicate_action_items();
    assert(!nothing.succeeded() && nothing.status() == FilesCommandStatus::InvalidArgument);
    assert(view_model.error_text() == nothing.message());
    assert(std::strcmp(lv_subject_get_string(view_model.error_subject()), nothing.message().c_str()) == 0);
    assert(view_model.screen() == FilesScreen::Select && view_model.selecting());
    assert(view_model.toggle_selection(find_presentation(view_model, "first.txt")).succeeded());
    assert(view_model.error_text().empty());
    assert(view_model.back().succeeded());
    assert(!view_model.selecting());

    // The selection menu names the selected items by their number.
    select({"first.txt", "second.txt"});
    assert(view_model.show(FilesScreen::SelectionActions).succeeded());
    assert(lv_subject_get_int(view_model.screen_subject()) == static_cast<std::int32_t>(FilesScreen::SelectionActions));
    assert(view_model.selecting() && view_model.action_count() == 2U);
    assert(view_model.action_entry() == nullptr);
    assert(view_model.copy_items_title() == "Copy 2 Items");
    assert(view_model.new_folder_with_items_title() == "New Folder with 2 Items");
    assert(view_model.back().succeeded());
    assert(view_model.screen() == FilesScreen::Select && view_model.selected_count() == 2U);

    // Move and Delete describe several items by their number and one item by its name.
    assert(view_model.show(FilesScreen::Move).succeeded());
    assert(view_model.move_hint() == "Choose a folder for 2 items.");
    assert(view_model.back().succeeded());
    assert(view_model.show(FilesScreen::ConfirmDelete).succeeded());
    assert(view_model.delete_title() == "Delete 2 Items?");
    assert(view_model.delete_message() == "These items will be deleted immediately. You can’t undo this action.");
    assert(view_model.back().succeeded());
    assert(view_model.toggle_selection(find_presentation(view_model, "second.txt")).succeeded());
    assert(view_model.show(FilesScreen::ConfirmDelete).succeeded());
    assert(view_model.delete_title() == "Delete “first.txt”?");
    assert(view_model.delete_message() == "This item will be deleted immediately. You can’t undo this action.");
    assert(view_model.back().succeeded());
    assert(view_model.show(FilesScreen::Move).succeeded());
    assert(view_model.move_hint() == "Choose a folder for “first.txt”.");
    assert(view_model.open_move_destination(0).succeeded());
    assert(view_model.can_move_here());
    assert(view_model.move_action_items().succeeded());
    assert(view_model.screen() == FilesScreen::Home && !view_model.selecting());
    assert(lv_subject_get_int(view_model.selected_count_subject()) == 0);
    assert(std::filesystem::exists(fixture.path("Inbox/first.txt")));

    // The long-press menu offers New Folder with 1 Item.
    assert(view_model.show_actions(find_presentation(view_model, "second.txt")).succeeded());
    assert(view_model.new_folder_with_items_title() == "New Folder with 1 Item");
    assert(view_model.back().succeeded());
}

}  // namespace

int main()
{
    lv_init();
    {
        test_commands_presentations_and_subjects();
        test_grid_presentations();
        test_sort_view_and_selection_commands();
        test_item_action_commands();
        test_selection_action_commands();
    }
    lv_deinit();
    return 0;
}
