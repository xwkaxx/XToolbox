/**
 * @file serial_output_panel.cpp
 * @brief 输出工具栏、连续文本排版和 UTF-8 选区交互。
 * 历史模型持有记录；本面板仅持有动画、选区及上帧布局缓存。
 */
#include "serial_output_panel.h"
#include "../application/serial_controller.h"
#include "serial_ui_widgets.h"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cstring>

using namespace SerialUiWidgets;

/// @brief 绘制原始缓存清除入口与确认弹窗；确认后冻结旧显示并清零计数。
void SerialOutputPanel::DrawClearButton()
{
    // 固定点击区域，与同排控件等高；动画只改变图标尺寸。
    const float size = ImGui::GetFrameHeight();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
    const bool clicked = ImGui::Button(
        icons_.clearIcon.view ? "##ClearReceive" : "清空##ClearReceive",
        ImVec2(icons_.clearIcon.view ? size : 0.0f, size));
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    const bool hovered = ImGui::IsItemHovered();
    const bool highlighted = hovered || ImGui::IsItemFocused();
    const float response = 1.0f - std::exp(-18.0f * ImGui::GetIO().DeltaTime);
    clear_hover_ += ((highlighted ? 1.0f : 0.0f) - clear_hover_) * response;
    clear_press_ += ((ImGui::IsItemActive() ? 1.0f : 0.0f) - clear_press_) * response;
    if (icons_.clearIcon.view)
    {
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec2 maximum = ImGui::GetItemRectMax();
        const ImVec2 center((minimum.x + maximum.x) * 0.5f,
            (minimum.y + maximum.y) * 0.5f);
        const float half = size * 0.44f * (1.0f + 0.10f * clear_hover_ - 0.10f * clear_press_);
        const float tint = 1.0f - 0.22f * clear_hover_;
        ImGui::GetWindowDrawList()->AddImage(ImTextureRef(icons_.clearIcon.view.Get()),
            ImVec2(center.x - half, center.y - half),
            ImVec2(center.x + half, center.y + half),
            ImVec2(0, 0), ImVec2(1, 1),
            ImGui::GetColorU32(ImVec4(tint, tint, tint, 1.0f)));
    }
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("清除原始数据缓存和计数，保留当前文字");
    // 点击图标只提出清空请求，确认后才修改接收记录。
    const char* popupId = "确认清除缓存##ConfirmClearReceive";
    if (clicked)
        ImGui::OpenPopup(popupId);

    const float scale = UiScale();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f * scale, 16.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(1, 1, 1, 1));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.92f, 0.95f, 0.98f, 1));
    if (ImGui::BeginPopupModal(popupId, nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::TextUnformatted("确定清除原始数据缓存并重置收发计数吗？");
        ImGui::TextDisabled("保留当前文字；旧文字不再切换 HEX/Abc 格式，不会关闭串口。");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 默认焦点放在取消上，Esc 也可关闭，避免误按回车清空。
        if (ImGui::Button("取消", ImVec2(100.0f * scale, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.42f, 0.72f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.08f, 0.49f, 0.81f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.04f, 0.34f, 0.61f, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        if (ImGui::Button("清空", ImVec2(100.0f * scale, 0)))
        {
            controller_.ClearRawData();
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(4);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}
/// @brief 切换后续记录的时间戳选项，保留已有记录的时间属性。
void SerialOutputPanel::DrawTimestampButton()
{
    const float size = ImGui::GetFrameHeight();
    const auto& icon = controller_.Preferences().show_timestamp ? icons_.timestampOnIcon : icons_.timestampOffIcon;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
    const bool clicked = ImGui::Button(icon.view ? "##Timestamp" : "时间##Timestamp",
        ImVec2(icon.view ? size : 0.0f, size));
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    const bool hovered = ImGui::IsItemHovered();
    const float target = ImGui::IsItemActive() ? -0.08f : hovered ? 0.08f : 0.0f;
    timestamp_emphasis_ += (target - timestamp_emphasis_) * (1.0f - std::exp(-18.0f * ImGui::GetIO().DeltaTime));
    if (icon.view)
    {
        DrawIconButtonSurface(controller_.Preferences().show_timestamp);
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec2 center(minimum.x + size * 0.5f, minimum.y + size * 0.5f);
        const float half = size * 0.38f * (1.0f + timestamp_emphasis_);
        ImGui::GetWindowDrawList()->AddImage(ImTextureRef(icon.view.Get()),
            ImVec2(center.x - half, center.y - half), ImVec2(center.x + half, center.y + half));
    }
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip(controller_.Preferences().show_timestamp ? "关闭时间戳（本机时间，精确到毫秒）"
            : "显示时间戳（本机时间，精确到毫秒）");
    if (clicked)
    {
        controller_.Preferences().show_timestamp = !controller_.Preferences().show_timestamp;
        controller_.SavePreferences();
        // 仅影响后续记录；既有记录不补加，也不移除时间戳。
    }
}

/// @brief 隐藏已显示文字，保留原始缓存及实际收发计数。
void SerialOutputPanel::DrawEraseTextButton()
{
    if (DrawEraserButton("##EraseOutputText", "清空收发区文字，保留原始缓存和收发计数"))
    {
        controller_.HideOutputText();
    }
}

/// @brief 根据可用宽度换行排列显示模式、记录开关和字号控件。
void SerialOutputPanel::DrawOutputToolbar(float width)
{
    const float size = ImGui::GetFrameHeight();
    const float textWidth = FormatButtonWidth();
    const float right = ImGui::GetCursorScreenPos().x + width;
    // 判断下一个控件是否越过右边界；不强制 SameLine 即使用 ImGui 的自然换行。
    const auto next = [&](float buttonWidth)
    {
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + buttonWidth <= right)
            ImGui::SameLine();
    };
    if (DrawIconButton(icons_, "##DisplayMode", controller_.Preferences().display_hex ? icons_.displayHexIcon : icons_.displayTextIcon, false,
        controller_.Preferences().display_hex ? "显示方式：十六进制；点击切换 UTF-8 字符串"
            : "显示方式：UTF-8 字符串；点击切换十六进制", textWidth))
    {
        controller_.Preferences().display_hex = !controller_.Preferences().display_hex;
        controller_.SavePreferences();
        controller_.History().InvalidateDisplay();
    }
    next(size);
    DrawTimestampButton();
    next(size);
    if (DrawIconButton(icons_, "##ShowReceive", controller_.Preferences().show_receive ? icons_.showReceiveIcon : icons_.showReceiveOffIcon, controller_.Preferences().show_receive,
        controller_.Preferences().show_receive ? "后续接收数据显示中；点击关闭，不影响已有记录" : "后续接收数据不显示；点击开启，不补显示历史", size, true))
    {
        controller_.Preferences().show_receive = !controller_.Preferences().show_receive;
        controller_.SavePreferences();
    }
    next(size);
    if (DrawIconButton(icons_, "##ShowSend", controller_.Preferences().show_send ? icons_.showSendIcon : icons_.showSendOffIcon, controller_.Preferences().show_send,
        controller_.Preferences().show_send ? "后续发送数据显示中；点击关闭，不影响已有记录" : "后续发送数据不显示；点击开启，不补显示历史", size, true))
    {
        controller_.Preferences().show_send = !controller_.Preferences().show_send;
        controller_.SavePreferences();
    }
    next(size);
    if (DrawIconButton(icons_, "##ResetOutputFont", icons_.fontResetIcon, false, "恢复输出区默认字号（100%）", size))
    {
        controller_.Preferences().output_font_scale = 1.0f;
        controller_.SavePreferences();
        controller_.History().InvalidateDisplay();
    }
    char tooltip[96];
    std::snprintf(tooltip, sizeof(tooltip), "放大输出文字 | 当前 %.0f%%", controller_.Preferences().output_font_scale * 100);
    next(size);
    ImGui::BeginDisabled(controller_.Preferences().output_font_scale >= 2.0f);
    if (DrawIconButton(icons_, "##IncreaseOutputFont", icons_.fontIncreaseIcon, false, tooltip, size))
    {
        controller_.Preferences().output_font_scale = (std::min)(2.0f, controller_.Preferences().output_font_scale + 0.1f);
        controller_.SavePreferences();
        controller_.History().InvalidateDisplay();
    }
    ImGui::EndDisabled();
    std::snprintf(tooltip, sizeof(tooltip), "缩小输出文字 | 当前 %.0f%%", controller_.Preferences().output_font_scale * 100);
    next(size);
    ImGui::BeginDisabled(controller_.Preferences().output_font_scale <= 0.6f);
    if (DrawIconButton(icons_, "##DecreaseOutputFont", icons_.fontDecreaseIcon, false, tooltip, size))
    {
        controller_.Preferences().output_font_scale = (std::max)(0.6f, controller_.Preferences().output_font_scale - 0.1f);
        controller_.SavePreferences();
        controller_.History().InvalidateDisplay();
    }
    ImGui::EndDisabled();
    // 工具栏空间足够时贴右，窄窗口则换行。
    if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + size <= right)
    {
        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImVec2(right - size, ImGui::GetCursorScreenPos().y));
    }
    DrawEraseTextButton();
}


/// @brief 按文本片段做命中、选择与复制，并绘制选区背景和文字。
/// @param selecting 返回是否正在选择或打开复制菜单，供调用方抑制自动滚动。
void SerialOutputPanel::DrawSelectableOutput(const std::string& text, const std::vector<OutputTextRun>& runs,
    float lineHeight, bool& selecting)
{
    // 新数据追加不打断选择；清空、裁剪或切换显示格式时取消旧选择。
    if (text.size() < previous_text_.size() || text.compare(0, previous_text_.size(), previous_text_) != 0)
    {
        anchor_ = caret_ = 0;
        dragging_ = false;
    }
    previous_text_ = text;
    anchor_ = (std::min)(anchor_, text.size());
    caret_ = (std::min)(caret_, text.size());
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    auto measure = [&](size_t first, size_t last) {
        return font->CalcTextSizeA(size, FLT_MAX, 0.0f,
            text.data() + first, text.data() + last).x;
    };
    // 选区索引统一使用 UTF-8 字节偏移；命中只停在字符边界，避免复制半个中文字符。
    auto hit = [&]() {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        float best = FLT_MAX;
        size_t offset = text.size();
        for (const auto& run : runs)
        {
            const float dy = mouse.y < run.position.y ? run.position.y - mouse.y
                : mouse.y > run.position.y + lineHeight ? mouse.y - run.position.y - lineHeight : 0.0f;
            float x = run.position.x;
            for (size_t i = run.begin;;)
            {
                // 先以行的垂直距离筛选，再比较行内水平距离，降低跨行拖选时跳到邻行的概率。
                const float score = dy * 100000.0f + std::fabs(mouse.x - x);
                if (score < best) { best = score; offset = i; }
                if (i == run.end) break;
                size_t next = i + 1;
                while (next < run.end && (static_cast<unsigned char>(text[next]) & 0xC0) == 0x80) ++next;
                x += measure(i, next);
                i = next;
            }
        }
        return offset;
    };
    const bool hovered = ImGui::IsWindowHovered();
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        ImGui::SetWindowFocus();
        caret_ = hit();
        if (!ImGui::GetIO().KeyShift) anchor_ = caret_;
        dragging_ = true;
    }
    if (dragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        caret_ = hit();
        const float mouseY = ImGui::GetIO().MousePos.y;
        const float top = ImGui::GetWindowPos().y;
        const float bottom = top + ImGui::GetWindowSize().y;
        const float speed = lineHeight * 20.0f * ImGui::GetIO().DeltaTime;
        if (mouseY < top) ImGui::SetScrollY(ImGui::GetScrollY() - speed);
        if (mouseY > bottom) ImGui::SetScrollY(ImGui::GetScrollY() + speed);
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragging_ = false;
    // 复制使用逻辑文本的连续区间；视觉自动折行不会向剪贴板额外插入换行符。
    auto copy = [&]() {
        const size_t first = (std::min)(anchor_, caret_), last = (std::max)(anchor_, caret_);
        if (first != last) ImGui::SetClipboardText(text.substr(first, last - first).c_str());
    };
    if (ImGui::IsWindowFocused() && ImGui::GetIO().KeyCtrl)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_A, false)) { anchor_ = 0; caret_ = text.size(); }
        if (ImGui::IsKeyPressed(ImGuiKey_C, false)) copy();
    }
    if (ImGui::BeginPopupContextWindow("OutputCopyMenu"))
    {
        if (ImGui::MenuItem("复制选中内容", "Ctrl+C", false, anchor_ != caret_)) copy();
        if (ImGui::MenuItem("全选", "Ctrl+A", false, !text.empty())) { anchor_ = 0; caret_ = text.size(); }
        if (ImGui::MenuItem("复制全部", nullptr, false, !text.empty()))
            ImGui::SetClipboardText(text.c_str());
        ImGui::EndPopup();
    }
    const size_t first = (std::min)(anchor_, caret_), last = (std::max)(anchor_, caret_);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (const auto& run : runs)
    {
        // 每段只绘制与选区相交的背景，随后绘制文字，保证高亮不会覆盖字形。
        const size_t a = (std::max)(first, run.begin), b = (std::min)(last, run.end);
        if (a < b)
        {
            const float x = run.position.x + measure(run.begin, a);
            draw->AddRectFilled(ImVec2(x, run.position.y),
                ImVec2(x + measure(a, b), run.position.y + lineHeight),
                ImGui::GetColorU32(ImVec4(0.25f, 0.57f, 0.94f, 0.25f)));
        }
        draw->AddText(run.position, run.color, text.data() + run.begin, text.data() + run.end);
    }
    selecting = dragging_ || anchor_ != caret_ || ImGui::IsPopupOpen("OutputCopyMenu");
}


/// @brief 绘制缓存操作及显示工具栏；由外层在输出区之前调用。
void SerialOutputPanel::DrawToolbar(float width)
{
    DrawClearButton();
    ImGui::Separator();
    DrawOutputToolbar(width);
    ImGui::Separator();
}

/// @brief 将可见记录排版为文本片段，再统一处理选择及自动滚动。
void SerialOutputPanel::Draw(float height)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    if (ImGui::BeginChild("ReceiveArea", ImVec2(0.0f, height)))
    {
        // 只改变输出区字体，退出 child 前恢复，不影响工具栏和串口参数。
        ImGui::PushFont(nullptr, style.FontSizeBase * controller_.Preferences().output_font_scale);
        const bool changed = controller_.History().RefreshDisplay(controller_.Preferences().display_hex);
        // 连续绘制按可用窗口宽度折行；不修改数据，也不按收发记录插入换行。
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImVec2 pen = origin;
        const float availableWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x);
        const float right = origin.x + availableWidth;

        const bool widthChanged = std::fabs(previous_width_ - availableWidth) > 0.5f;
        previous_width_ = availableWidth;
        ImGui::SetScrollX(0.0f);
        ImFont* font = ImGui::GetFont();
        const float fontSize = ImGui::GetFontSize();
        const float lineHeight = ImGui::GetTextLineHeight();
        // copyText 是逻辑文本，textRuns 保存其字节区间和屏幕位置；两者分离才能支持缩放后重排。
        std::string copyText;
        std::vector<OutputTextRun> textRuns;
        bool anyVisible = false;
        for (const auto& record : controller_.History().Records())
        {
            if (!record.visible)
                continue;
            anyVisible = true;
            const ImU32 color = ImGui::GetColorU32(record.error
                ? ImVec4(0.75f, 0.19f, 0.13f, 1)
                : record.transmit ? ImVec4(0.06f, 0.37f, 0.72f, 1)
                : ImVec4(0.05f, 0.43f, 0.28f, 1));
            // 每条时间戳记录从新行开始；关闭时间戳后的新数据继续连续显示。
            if ((record.error || record.has_timestamp) && pen.x != origin.x)
            {
                copyText += '\n';
                pen.x = origin.x;
                pen.y += lineHeight;
            }
            char timestamp[24] = {};
            if (record.has_timestamp)
                std::snprintf(timestamp, sizeof(timestamp), "[%02u:%02u:%02u.%03u] ",
                    static_cast<unsigned>(record.time.wHour), static_cast<unsigned>(record.time.wMinute),
                    static_cast<unsigned>(record.time.wSecond), static_cast<unsigned>(record.time.wMilliseconds));
            // 按记录自身的标记显示时间戳，不追溯修改历史数据。
            const std::string display = std::string(timestamp) + record.display;
            const size_t displayOffset = copyText.size();
            copyText += display;
            const char* begin = display.data();
            const char* end = begin + display.size();
            const char* dataBegin = begin + std::strlen(timestamp);
            const bool hex = !record.error && controller_.Preferences().display_hex;
            while (begin < end)
            {
                if (*begin == '\n')
                {
                    pen.x = origin.x;
                    pen.y += lineHeight;
                    ++begin;
                    continue;
                }
                const char* lineEnd = begin;
                float lineWidth = 0.0f;
                while (lineEnd < end && *lineEnd != '\n')
                {
                    // HEX 按完整字节折行；界面错误提示仍包含中文，折行需保留 UTF-8 字符边界。
                    const char* next = lineEnd + 1;
                    if (hex && lineEnd >= dataBegin)
                    {
                        while (next < end && *next != ' ' && *next != '\n') ++next;
                        if (next < end && *next == ' ') ++next;
                    }
                    else
                    {
                        while (next < end && (static_cast<unsigned char>(*next) & 0xC0) == 0x80)
                            ++next;
                    }
                    const float tokenWidth = font->CalcTextSizeA(fontSize, FLT_MAX,
                        0.0f, lineEnd, next).x;
                    if (pen.x + lineWidth + tokenWidth > right && (pen.x > origin.x || lineEnd > begin))
                        break;
                    lineWidth += tokenWidth;
                    lineEnd = next;
                }
                // 当前行剩余空间不够时移到下一行；空行允许放入超宽单元，保证循环仍能前进。
                if (lineEnd == begin)
                {
                    pen.x = origin.x;
                    pen.y += lineHeight;
                    continue;
                }
                textRuns.push_back({ pen, color,
                    displayOffset + static_cast<size_t>(begin - display.data()),
                    displayOffset + static_cast<size_t>(lineEnd - display.data()) });
                pen.x += lineWidth;
                begin = lineEnd;
                if (begin < end && *begin != '\n')
                {
                    pen.x = origin.x;
                    pen.y += lineHeight;
                }
            }
            if ((record.error || record.has_timestamp) && pen.x != origin.x)
            {
                copyText += '\n';
                pen.x = origin.x;
                pen.y += lineHeight;
            }
        }
        if (anyVisible)
            // 直接 DrawList 绘制不会扩展内容尺寸，Dummy 显式建立滚动范围。
            ImGui::Dummy(ImVec2(0.0f, pen.y - origin.y + lineHeight));
        bool selecting = false;
        DrawSelectableOutput(copyText, textRuns, lineHeight, selecting);
        // 选择或复制期间维持用户位置，防止新数据到达把选区拖离视野。
        if (!selecting && (changed || widthChanged) && anyVisible)
            ImGui::SetScrollHereY(1.0f);
        ImGui::PopFont();
    }
    ImGui::EndChild();
}
