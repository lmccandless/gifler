#include "gifler_editor/EditorWindow.h"

#include "gifler_editor/EditorModel.h"

#include <Windows.h>
#include <windowsx.h>

#include <algorithm>
#include <format>
#include <memory>
#include <string>

namespace gifler::editor {
namespace {

constexpr wchar_t EditorClassName[] = L"GiflerNativeEditorWindow";
constexpr int IdSetStart = 2101;
constexpr int IdSetEnd = 2102;
constexpr int IdDeleteFrame = 2103;
constexpr int IdDeleteEven = 2104;
constexpr int IdReset = 2105;
constexpr int IdApply = 2106;

HFONT default_gui_font() {
    return reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

void set_control_font(HWND control) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(default_gui_font()), TRUE);
}

} // namespace

class EditorWindowImpl {
public:
    EditorWindowImpl(std::vector<gifler::core::BgraFrame> frames, EditorWindow::ApplyCallback onApply)
        : frames_(std::move(frames)), model_(frames_.size()), onApply_(std::move(onApply)) {}

    bool create(HWND owner) {
        HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = EditorWindowImpl::static_proc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = EditorClassName;
        RegisterClassExW(&wc);

        hwnd_ = CreateWindowExW(WS_EX_APPWINDOW, EditorClassName, L"Gifler Editor", WS_OVERLAPPEDWINDOW | WS_VSCROLL,
                                CW_USEDEFAULT, CW_USEDEFAULT, 840, 560, owner, nullptr, instance, this);
        if (hwnd_ == nullptr) {
            return false;
        }
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        return true;
    }

private:
    static LRESULT CALLBACK static_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        EditorWindowImpl* self = nullptr;
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<EditorWindowImpl*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        } else {
            self = reinterpret_cast<EditorWindowImpl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (self != nullptr) {
            return self->proc(message, wParam, lParam);
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT proc(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_CREATE:
            create_controls();
            layout();
            return 0;
        case WM_SIZE:
            layout();
            return 0;
        case WM_COMMAND:
            handle_command(LOWORD(wParam));
            return 0;
        case WM_LBUTTONDOWN:
            select_from_point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_VSCROLL:
            handle_scroll(LOWORD(wParam), HIWORD(wParam));
            return 0;
        case WM_PAINT:
            paint();
            return 0;
        case WM_DESTROY:
            delete this;
            return 0;
        default:
            return DefWindowProcW(hwnd_, message, wParam, lParam);
        }
    }

    void create_controls() {
        setStartButton_ = CreateWindowW(L"BUTTON", L"Set Start", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 1, 1, hwnd_,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdSetStart)), nullptr, nullptr);
        setEndButton_ = CreateWindowW(L"BUTTON", L"Set End", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 1, 1, hwnd_,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdSetEnd)), nullptr, nullptr);
        deleteButton_ = CreateWindowW(L"BUTTON", L"Delete", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 1, 1, hwnd_,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdDeleteFrame)), nullptr, nullptr);
        deleteEvenButton_ = CreateWindowW(L"BUTTON", L"Delete Even", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 1, 1, hwnd_,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdDeleteEven)), nullptr, nullptr);
        resetButton_ = CreateWindowW(L"BUTTON", L"Reset", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 1, 1, hwnd_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdReset)), nullptr, nullptr);
        applyButton_ = CreateWindowW(L"BUTTON", L"Apply", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 0, 0, 1, 1, hwnd_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdApply)), nullptr, nullptr);
        for (HWND control : {setStartButton_, setEndButton_, deleteButton_, deleteEvenButton_, resetButton_, applyButton_}) {
            set_control_font(control);
        }
    }

    void layout() {
        RECT client{};
        GetClientRect(hwnd_, &client);
        int x = 10;
        auto place = [&](HWND control, int width) {
            MoveWindow(control, x, 8, width, 28, TRUE);
            x += width + 8;
        };
        place(setStartButton_, 82);
        place(setEndButton_, 76);
        place(deleteButton_, 72);
        place(deleteEvenButton_, 104);
        place(resetButton_, 72);
        place(applyButton_, 72);
        update_scrollbar(client);
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void update_scrollbar(RECT client) {
        const int timelineTop = timeline_top(client);
        const int availableRows = std::max(1, (static_cast<int>(client.bottom) - timelineTop - 10) / rowHeight_);
        const int rows = static_cast<int>((frames_.size() + columns_ - 1) / columns_);
        maxScrollRow_ = std::max(0, rows - availableRows);
        scrollRow_ = std::clamp(scrollRow_, 0, maxScrollRow_);

        SCROLLINFO info{};
        info.cbSize = sizeof(info);
        info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        info.nMin = 0;
        info.nMax = std::max(0, rows - 1);
        info.nPage = static_cast<UINT>(availableRows);
        info.nPos = scrollRow_;
        SetScrollInfo(hwnd_, SB_VERT, &info, TRUE);
    }

    void handle_command(int id) {
        if (frames_.empty()) {
            return;
        }
        switch (id) {
        case IdSetStart:
            model_.set_trim_start(selectedIndex_);
            break;
        case IdSetEnd:
            model_.set_trim_end(selectedIndex_);
            break;
        case IdDeleteFrame:
            model_.delete_frame(selectedIndex_);
            break;
        case IdDeleteEven:
            model_.delete_even_frames();
            break;
        case IdReset:
            model_.reset(frames_.size());
            selectedIndex_ = 0;
            break;
        case IdApply:
            apply_edits();
            break;
        default:
            break;
        }
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void apply_edits() {
        auto edited = materialize_edited_frames();
        if (edited.empty()) {
            MessageBoxW(hwnd_, L"No frames are included in the edited range.", L"Gifler Editor", MB_OK | MB_ICONWARNING);
            return;
        }
        if (onApply_) {
            onApply_(std::move(edited));
        }
        SetWindowTextW(hwnd_, L"Gifler Editor - Applied");
    }

    [[nodiscard]] std::vector<gifler::core::BgraFrame> materialize_edited_frames() const {
        std::vector<gifler::core::BgraFrame> edited;
        for (std::size_t index : model_.included_indices()) {
            edited.push_back(frames_[index]);
        }
        return edited;
    }

    void handle_scroll(int request, int position) {
        switch (request) {
        case SB_LINEUP:
            --scrollRow_;
            break;
        case SB_LINEDOWN:
            ++scrollRow_;
            break;
        case SB_PAGEUP:
            scrollRow_ -= 4;
            break;
        case SB_PAGEDOWN:
            scrollRow_ += 4;
            break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
            scrollRow_ = position;
            break;
        default:
            break;
        }
        scrollRow_ = std::clamp(scrollRow_, 0, maxScrollRow_);
        SetScrollPos(hwnd_, SB_VERT, scrollRow_, TRUE);
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void select_from_point(int x, int y) {
        RECT client{};
        GetClientRect(hwnd_, &client);
        const int top = timeline_top(client);
        if (y < top || x < 10) {
            return;
        }
        const int column = (x - 10) / cellWidth_;
        const int row = (y - top) / rowHeight_;
        if (column < 0 || column >= columns_ || row < 0) {
            return;
        }
        const std::size_t index = static_cast<std::size_t>((scrollRow_ + row) * columns_ + column);
        if (index < frames_.size()) {
            selectedIndex_ = index;
            InvalidateRect(hwnd_, nullptr, TRUE);
        }
    }

    void paint() {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd_, &ps);
        RECT client{};
        GetClientRect(hwnd_, &client);
        FillRect(dc, &client, GetSysColorBrush(COLOR_WINDOW));
        SelectObject(dc, default_gui_font());
        SetBkMode(dc, TRANSPARENT);

        paint_summary(dc, client);
        paint_preview(dc, client);
        paint_timeline(dc, client);

        EndPaint(hwnd_, &ps);
    }

    void paint_summary(HDC dc, RECT client) {
        const auto included = model_.included_indices();
        RECT text{480, 8, client.right - 10, 38};
        const std::wstring summary = std::format(L"Selected {} | trim {}-{} | output {} / {} frames", selectedIndex_ + 1,
                                                 model_.trim_start() + 1, model_.trim_end() + 1, included.size(),
                                                 frames_.size());
        DrawTextW(dc, summary.c_str(), static_cast<int>(summary.size()), &text, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    }

    void paint_preview(HDC dc, RECT client) {
        RECT preview{10, 48, client.right - 10, 260};
        FillRect(dc, &preview, GetSysColorBrush(COLOR_BTNFACE));
        FrameRect(dc, &preview, GetSysColorBrush(COLOR_WINDOWFRAME));
        if (frames_.empty()) {
            return;
        }
        draw_frame(dc, frames_[selectedIndex_], preview);
    }

    void paint_timeline(HDC dc, RECT client) {
        const int top = timeline_top(client);
        RECT band{0, top - 8, client.right, client.bottom};
        FillRect(dc, &band, GetSysColorBrush(COLOR_BTNFACE));

        const int availableRows = std::max(1, (static_cast<int>(client.bottom) - top - 10) / rowHeight_);
        const std::size_t first = static_cast<std::size_t>(scrollRow_ * columns_);
        const std::size_t visible = static_cast<std::size_t>(availableRows * columns_);
        const std::size_t last = std::min(frames_.size(), first + visible);

        for (std::size_t index = first; index < last; ++index) {
            const int local = static_cast<int>(index - first);
            const int row = local / columns_;
            const int col = local % columns_;
            RECT cell{10 + col * cellWidth_, top + row * rowHeight_, 10 + col * cellWidth_ + cellWidth_ - 8,
                      top + row * rowHeight_ + rowHeight_ - 8};
            paint_cell(dc, cell, index);
        }
    }

    void paint_cell(HDC dc, RECT cell, std::size_t index) {
        HBRUSH fill = GetSysColorBrush(model_.included(index) ? COLOR_WINDOW : COLOR_BTNFACE);
        FillRect(dc, &cell, fill);
        FrameRect(dc, &cell, GetSysColorBrush(index == selectedIndex_ ? COLOR_HIGHLIGHT : COLOR_WINDOWFRAME));

        RECT thumb = cell;
        thumb.bottom -= 18;
        InflateRect(&thumb, -4, -4);
        draw_frame(dc, frames_[index], thumb);

        RECT label{cell.left + 4, cell.bottom - 18, cell.right - 4, cell.bottom - 2};
        const std::wstring text = model_.deleted(index) ? std::format(L"{} del", index + 1) : std::to_wstring(index + 1);
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &label, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    }

    void draw_frame(HDC dc, const gifler::core::BgraFrame& frame, RECT dest) {
        if (frame.empty()) {
            return;
        }
        const int destW = dest.right - dest.left;
        const int destH = dest.bottom - dest.top;
        if (destW <= 0 || destH <= 0) {
            return;
        }

        const double scale = std::min(static_cast<double>(destW) / static_cast<double>(frame.width),
                                      static_cast<double>(destH) / static_cast<double>(frame.height));
        const int drawW = std::max(1, static_cast<int>(static_cast<double>(frame.width) * scale));
        const int drawH = std::max(1, static_cast<int>(static_cast<double>(frame.height) * scale));
        const int drawX = dest.left + (destW - drawW) / 2;
        const int drawY = dest.top + (destH - drawH) / 2;

        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = frame.width;
        info.bmiHeader.biHeight = -frame.height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        SetStretchBltMode(dc, HALFTONE);
        StretchDIBits(dc, drawX, drawY, drawW, drawH, 0, 0, frame.width, frame.height, frame.pixels.data(), &info,
                      DIB_RGB_COLORS, SRCCOPY);
    }

    int timeline_top(RECT) const { return 280; }

    HWND hwnd_ = nullptr;
    std::vector<gifler::core::BgraFrame> frames_{};
    EditorModel model_{};
    EditorWindow::ApplyCallback onApply_{};
    std::size_t selectedIndex_ = 0;
    int scrollRow_ = 0;
    int maxScrollRow_ = 0;
    static constexpr int columns_ = 5;
    static constexpr int cellWidth_ = 154;
    static constexpr int rowHeight_ = 104;
    HWND setStartButton_ = nullptr;
    HWND setEndButton_ = nullptr;
    HWND deleteButton_ = nullptr;
    HWND deleteEvenButton_ = nullptr;
    HWND resetButton_ = nullptr;
    HWND applyButton_ = nullptr;
};

void EditorWindow::show_placeholder(HWND owner) {
    MessageBoxW(owner, L"No recording is available to edit.", L"Gifler Editor", MB_OK | MB_ICONINFORMATION);
}

void EditorWindow::show(HWND owner, const std::vector<gifler::core::BgraFrame>& frames, ApplyCallback onApply) {
    if (frames.empty()) {
        show_placeholder(owner);
        return;
    }
    auto* window = new EditorWindowImpl(frames, std::move(onApply));
    if (!window->create(owner)) {
        delete window;
        MessageBoxW(owner, L"Could not create the editor window.", L"Gifler Editor", MB_OK | MB_ICONERROR);
    }
}

} // namespace gifler::editor
