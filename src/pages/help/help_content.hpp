#ifndef LILYGO_UI_FILES_PAGES_HELP_HELP_CONTENT_HPP
#define LILYGO_UI_FILES_PAGES_HELP_HELP_CONTENT_HPP

#include <array>

namespace help_content {

struct Section {
    const char *title;
    const char *body;
};

// Keep this copy self-contained so the view only renders product guidance. The
// content deliberately describes commands using the labels shown by FilesView.
inline constexpr std::array<Section, 6> sections{{
    {"Browse files and folders",
     "Tap a folder to open it. Tap a file to open its read-only preview. Use the back button in the navigation bar to return to the parent folder. The root folder is the first folder shown by Files."},
    {"Search and sort",
     "Tap Search to find items in the current folder. Search does not scan inside subfolders. Open the More menu to switch between Icons and List views or to sort by Name, Kind, Date, or Size."},
    {"Select items",
     "Choose Select from the More menu, then tap items to select or clear them. Select All selects every visible item. Use the bottom toolbar to duplicate, move, or delete the selected items, and tap Done when you finish."},
    {"Copy, move, and organize",
     "Long-press a file or folder for actions such as Copy, Move, Get Info, Rename, Compress, Duplicate, and Delete. After copying, open the More menu and choose Paste in the destination folder. New Folder creates a folder in the current location."},
    {"Keyboard shortcuts",
     "Use Tab to move between controls and the arrow keys to move through files and folders. On this Help page, both keys move through the guidance sections and keep the focused section visible. Press Enter to open the focused item. Press Escape to close an open menu or return from a page."},
    {"Safety and previews",
     "Delete permanently removes the selected items after confirmation. Rename and Move never overwrite an existing item. Text previews are read-only and show up to 8 KiB; unsupported files remain available in the folder without a preview."},
}};

}  // namespace help_content

#endif
