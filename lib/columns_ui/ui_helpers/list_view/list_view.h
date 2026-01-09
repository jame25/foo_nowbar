#pragma once

#include "list_view_renderer.h"

namespace uih {

namespace lv {

struct SavedScrollPosition {
    int32_t previous_item_index{};
    int32_t next_item_index{};
    double proportional_position{};
};

} // namespace lv

class ListView {
public:
    static constexpr short IDC_HEADER = 1001;
    static constexpr short IDC_TOOLTIP = 1002;
    static constexpr short IDC_INLINEEDIT = 667;
    static constexpr short IDC_SEARCHBOX = 668;

    static constexpr unsigned MSG_KILL_INLINE_EDIT = WM_USER + 3;

    enum {
        TIMER_SCROLL_UP = 1001,
        TIMER_SCROLL_DOWN = 1002,
        TIMER_END_SEARCH,
        EDIT_TIMER_ID,
        TIMER_BASE
    };

    using ColourData = lv::ColourData;

    class Column {
    public:
        pfc::string8 m_title;
        int m_size{0};
        int m_display_size{0};
        int m_autosize_weight{1};
        alignment m_alignment{ALIGN_LEFT};

        Column(const char* title, int cx, int p_autosize_weight = 1, alignment alignment = ALIGN_LEFT)
            : m_title(title)
            , m_size(cx)
            , m_display_size(cx)
            , m_autosize_weight(p_autosize_weight)
            , m_alignment(alignment)
        {
        }

        Column() = default;
    };

    using string_array = std::vector<pfc::string_simple>;

    class InsertItem {
    public:
        string_array m_subitems;
        string_array m_groups;

        InsertItem() = default;

        InsertItem(const string_array& text, const string_array& p_groups)
        {
            m_subitems = text;
            m_groups = p_groups;
        }
        InsertItem(size_t subitem_count, size_t group_count)
        {
            m_subitems.resize(subitem_count);
            m_groups.resize(group_count);
        }
    };

    class ItemTransaction {
    public:
        ~ItemTransaction() noexcept;

        void insert_items(size_t index_start, size_t count, const InsertItem* items);
        void remove_items(const pfc::bit_array& mask);

    private:
        ItemTransaction(ListView& list_view) : m_list_view(list_view) {}

        ItemTransaction(const ItemTransaction&) = delete;
        ItemTransaction& operator=(const ItemTransaction&) = delete;

        ItemTransaction(ItemTransaction&&) = delete;
        ItemTransaction& operator=(ItemTransaction&&) = delete;

        std::optional<size_t> m_start_index;
        ListView& m_list_view;

        friend class ListView;
    };

    template <size_t subitem_count, size_t group_count>
    class SizedInsertItem : public InsertItem {
    public:
        SizedInsertItem() : InsertItem(subitem_count, group_count) {}
    };

    enum class SelectionMode {
        Multiple,
        /** Single selection, right-click changes selection, can deselect all items */
        SingleRelaxed,
        /** Single selection, right-click doesn't change the selection, can't deselect all items */
        SingleStrict
    };

    enum class HitTestCategory {
        NotOnItem,
        AboveViewport,
        BelowViewport,
        OnUnobscuredItem,
        OnItemObscuredAbove,
        OnItemObscuredBelow,
        LeftOfItem,
        RightOfItem,
        OnGroupHeader,
        LeftOfGroupHeader,
        RightOfGroupHeader,
    };

    struct HitTestResult {
        HitTestCategory category{};
        size_t index{};
        size_t insertion_index{};
        size_t group_level{};
        size_t column{};
    };

    enum class ItemVisibility {
        FullyVisible = 1,
        ObscuredAbove,
        ObscuredBelow,
        AboveViewport,
        BelowViewport,
    };

    enum class EnsureVisibleMode {
        PreferCentringItem = 1,
        PreferMinimalScrolling = 2,
    };

    ListView(std::unique_ptr<lv::RendererBase> renderer = std::make_unique<lv::DefaultRenderer>())
        : m_renderer{std::move(renderer)}
    {
        m_dragging_initial_point.x = 0;
        m_dragging_initial_point.y = 0;
        m_dragging_rmb_initial_point.x = 0;
        m_dragging_rmb_initial_point.y = 0;

        m_vertical_item_padding = scale_dpi_value(4);

        auto window_config = ContainerWindowConfig{L"NGLV"};
        window_config.window_ex_styles = 0u;
        window_config.window_styles = WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_TABSTOP | WS_BORDER;
        window_config.class_styles = CS_DBLCLKS | CS_HREDRAW;
        m_container_window = std::make_unique<ContainerWindow>(
            window_config, [this](auto&&... args) { return on_message(std::forward<decltype(args)>(args)...); });
    }

    /**
     * \brief               Creates a new list view window
     * \param wnd_parent    Handle of the parent window
     * \param window_pos    Initial window position
     * \return              Window handle of the created list view
     */
    HWND create(HWND wnd_parent, WindowPosition window_pos = {}, bool use_dialog_units = false)
    {
        return m_container_window->create(wnd_parent, window_pos, nullptr, use_dialog_units);
    }

    /**
     * \brief   Destroys the list view window.
     */
    void destroy() { m_container_window->destroy(); }

    /**
     * \brief   Gets the list view window handle.
     * \return  List view window handle
     */
    [[nodiscard]] HWND get_wnd() const { return m_container_window->get_wnd(); }

    int calculate_header_height();
    void set_columns(std::vector<Column> columns);
    void set_column_widths(std::vector<int> widths);
    void set_group_count(size_t count, bool b_update_columns = true);

    [[nodiscard]] size_t get_group_count() const { return m_group_count; }

    int get_columns_width();
    int get_columns_display_width();
    int get_column_display_width(size_t index);
    alignment get_column_alignment(size_t index);
    size_t get_column_count();

    lv::SavedScrollPosition save_scroll_position() const;
    void restore_scroll_position(const lv::SavedScrollPosition& position);

    void _set_scroll_position(int val) { m_scroll_position = val; }

    [[nodiscard]] int _get_scroll_position() const { return m_scroll_position; }

    void set_show_header(bool new_value);
    void set_show_tooltips(bool b_val);
    void set_limit_tooltips_to_clipped_items(bool b_val);
    void set_autosize(bool b_val);
    void set_always_show_focus(bool b_val);

    void set_variable_height_items(bool b_variable_height_items) { m_variable_height_items = b_variable_height_items; }

    void set_selection_mode(SelectionMode mode) { m_selection_mode = mode; }

    void set_alternate_selection_model(bool b_alternate_selection) { m_alternate_selection = b_alternate_selection; }

    void set_allow_header_rearrange(bool b_allow_header_rearrange)
    {
        m_allow_header_rearrange = b_allow_header_rearrange;
    }

    void set_use_dark_mode(bool use_dark_mode);
    void set_dark_edit_colours(COLORREF text_colour, COLORREF background_colour)
    {
        m_dark_edit_text_colour = text_colour;
        m_dark_edit_background_colour = background_colour;
    }
    void set_vertical_item_padding(int val);
    void set_font_from_log_font(const LOGFONT& log_font);
    void set_font(std::optional<direct_write::TextFormat> text_format, const LOGFONT& log_font);
    void set_group_font(std::optional<direct_write::TextFormat> text_format);
    void set_header_font(std::optional<direct_write::TextFormat> text_format, const LOGFONT& log_font);
    void set_sorting_enabled(bool b_val);
    void set_show_sort_indicators(bool b_val);
    void set_edge_style(uint32_t b_val);

    void on_size(bool b_update_scroll = true);
    void on_size(int cx, int cy, bool b_update_scroll = true);

    void reposition_header();

    void update_column_sizes();

    ItemTransaction start_transaction();
    void insert_items(size_t index_start, size_t count, const InsertItem* items,
        const std::optional<lv::SavedScrollPosition>& saved_scroll_position = std::nullopt);

    template <class TItems>
    void replace_items(size_t index_start, const TItems& items)
    {
        replace_items(index_start, items.get_size(), items.get_ptr());
    }
    void replace_items(size_t index_start, size_t count, const InsertItem* items);
    void remove_item(size_t index);
    void remove_items(const pfc::bit_array& mask);
    void remove_all_items();

    void hit_test_ex(POINT pt_client, HitTestResult& result);
    void update_scroll_info(bool b_vertical = true, bool b_horizontal = true, bool redraw = true,
        std::optional<int> new_vertical_position = std::nullopt);
    ItemVisibility get_item_visibility(size_t index);
    bool is_partially_visible(size_t index);
    bool is_fully_visible(size_t index);

    void ensure_visible(size_t index, EnsureVisibleMode mode = EnsureVisibleMode::PreferCentringItem);

    void scroll(int position, bool b_horizontal = false, bool suppress_scroll_window = false);
    void scroll_from_scroll_bar(short scroll_bar_command, bool b_horizontal = false);

    void get_item_group(size_t index, size_t level, size_t& index_start, size_t& count);
    void set_insert_mark(size_t index);
    void remove_insert_mark();

    void set_highlight_item(size_t index);
    void remove_highlight_item();
    void set_highlight_selected_item(size_t index);
    void remove_highlight_selected_item();

    /** Rect relative to main window client area */
    void get_header_rect(LPRECT rc) const;

    /** Current height*/
    [[nodiscard]] int get_header_height() const;
    [[nodiscard]] RECT get_items_rect() const;
    [[nodiscard]] int get_item_area_height() const;

    [[nodiscard]] int get_items_top() const { return get_items_rect().top; }

    void get_search_box_rect(LPRECT rc) const;
    [[nodiscard]] int get_search_box_height() const;

    void invalidate_all(bool b_children = false, bool non_client = false);
    void invalidate_items(size_t index, size_t count) const;

    void invalidate_items(const pfc::bit_array& mask);
    void invalidate_item_group_info_area(size_t index);

    void update_items(size_t index, size_t count);
    void update_all_items();

    // Current implementation clears sub-items.
    void reorder_items_partial(size_t base, const size_t* order, size_t count, bool update_focus_item = true);

    enum class VerticalPositionCategory {
        OnItem,
        OnGroupHeader,
        BetweenItems,
        NoItems,
    };

    struct VerticalHitTestResult {
        VerticalPositionCategory position_category{};
        /** The item at the y-position if position_category == OnItem, or the item before it if position_category ==
         * BetweenItems */
        int item_leftmost{};
        /** The item at the y-position if position_category == OnItem, or the item after it if position_category ==
         * BetweenItems */
        int item_rightmost{};
    };

    [[nodiscard]] VerticalHitTestResult vertical_hit_test(int y) const;

    [[nodiscard]] int get_item_at_or_before(int y_position) const
    {
        return vertical_hit_test(y_position).item_leftmost;
    }
    [[nodiscard]] int get_item_at_or_after(int y_position) const
    {
        return vertical_hit_test(y_position).item_rightmost;
    }

    int get_first_viewable_item();
    int get_last_viewable_item();
    int get_default_item_height();
    int get_default_group_height();

    [[nodiscard]] int get_item_height() const { return m_item_height; }

    [[nodiscard]] int get_item_height(size_t index) const
    {
        int ret = 1;
        if (m_variable_height_items && index < m_items.size())
            ret = m_items[index]->m_line_count * get_item_height();
        else
            ret = get_item_height();
        return ret;
    }

    void clear_all_items()
    {
        m_items.clear();
        PostMessage(get_wnd(), MSG_KILL_INLINE_EDIT, 0, 0);
    }

    [[nodiscard]] int get_item_group_header_total_height(size_t index) const
    {
        if (index >= m_items.size())
            return 0;

        return gsl::narrow<int>(get_item_display_group_count(index)) * m_group_height;
    }

    [[nodiscard]] int get_item_position(size_t index, bool b_include_headers = false) const
    {
        if (index >= m_items.size())
            return 0;

        const int position = m_items[index]->m_display_position;

        if (b_include_headers)
            return position - get_item_group_header_total_height(index);

        return position;
    }

    [[nodiscard]] int get_item_position_bottom(size_t index) const
    {
        if (index >= m_items.size())
            return 0;

        return get_item_position(index) + get_item_height(index);
    }

    int get_group_minimum_inner_height() { return get_show_group_info_area() ? get_group_info_area_total_height() : 0; }

    int get_item_group_bottom(size_t index, bool b_include_headers = false)
    {
        size_t gstart = index;
        size_t gcount = 0;
        get_item_group(index, m_group_count ? m_group_count - 1 : 0, gstart, gcount);
        int ret = 0;
        if (gcount)
            index = gstart + gcount - 1;
        ret += get_item_position(index, b_include_headers);
        ret += m_item_height - 1;
        if (get_show_group_info_area() && m_group_count) {
            int gheight = gsl::narrow<int>(gcount) * m_item_height;
            int group_cy = get_group_info_area_total_height();
            if (gheight < group_cy)
                ret += group_cy - gheight;
        }
        return ret;
    }

    void refresh_item_positions();

    enum notification_source_t {
        notification_source_unknown,
        notification_source_rmb,
    };

    bool copy_selected_items_as_text(size_t default_single_item_column = pfc_infinite);

    void get_selection_state(pfc::bit_array_var& out);
    void set_selection_state(const pfc::bit_array& p_affected, const pfc::bit_array& p_status, bool b_notify = true,
        notification_source_t p_notification_source = notification_source_unknown);
    size_t get_focus_item();
    void set_focus_item(size_t index, bool b_notify = true);
    bool get_item_selected(size_t index);

    bool is_range_selected(size_t index, size_t count)
    {
        size_t i;
        for (i = 0; i < count; i++)
            if (!get_item_selected(i + index))
                return false;
        return count != 0;
    }

    size_t get_selection_count(size_t max = pfc_infinite);

    size_t get_selected_item_single()
    {
        size_t num_selected = get_selection_count(2);
        size_t index = 0;
        if (num_selected == 1) {
            pfc::bit_array_bittable mask(get_item_count());
            get_selection_state(mask);
            size_t count = get_item_count();
            while (index < count) {
                if (mask[index])
                    break;
                index++;
            }
        } else
            index = pfc_infinite;
        return index;
    }

    void sort_by_column(size_t index, bool b_descending, bool b_selection_only = false)
    {
        notify_sort_column(index, b_descending, b_selection_only);
        if (!b_selection_only)
            set_sort_column(index, b_descending);
    }

    [[nodiscard]] std::optional<size_t> get_sort_column() const { return m_sort_column_index; }
    [[nodiscard]] bool get_sort_direction() const { return m_sort_direction; }

    void update_item_data(size_t index)
    {
        notify_update_item_data(index);
        // if (m_variable_height_items)
        {
            // m_items[index]->update_line_count();
            //__calculate_item_positions(index+1);
            // scrollbars?
        }
    }

    void set_item_selected(size_t index, bool b_state);
    void set_item_selected_single(
        size_t index, bool b_notify = true, notification_source_t p_notification_source = notification_source_unknown);

    bool disable_redrawing();
    void enable_redrawing();

    auto suspend_ensure_visible()
    {
        m_ensure_visible_suspended = true;
        return gsl::finally([this]() { m_ensure_visible_suspended = false; });
    }

    const char* get_item_text(size_t index, size_t column);

    size_t get_item_count() { return m_items.size(); }

    void activate_inline_editing(size_t column_start = 0);
    void activate_inline_editing(const pfc::list_base_const_t<size_t>& indices, size_t column);
    void activate_inline_editing(size_t index, size_t column);

    void create_timer_scroll_up();
    void create_timer_scroll_down();
    void destroy_timer_scroll_up();
    void destroy_timer_scroll_down();

    enum inline_edit_flags_t {
        inline_edit_uppercase = 1 << 0,
        inline_edit_autocomplete = 1 << 1,
    };

protected:
    enum EdgeStyle {
        edge_none,
        edge_sunken,
        edge_grey,
        edge_solid,
    };

    class Item;
    class Group;

    using t_group_ptr = pfc::refcounted_object_ptr_t<Group>;
    using t_item_ptr = pfc::refcounted_object_ptr_t<Item>;

    class Group : public pfc::refcounted_object_root {
    public:
        pfc::string8 m_text;
    };

    class Item : public pfc::refcounted_object_root {
    public:
        t_uint8 m_line_count{1};
        string_array m_subitems;
        std::vector<t_group_ptr> m_groups;

        size_t m_display_index{0};
        int m_display_position{0};
        bool m_selected{false};

        void update_line_count()
        {
            m_line_count = 1;
            for (auto&& subitem : m_subitems) {
                t_uint8 lc = 1;
                const char* ptr = subitem.c_str();
                while (*ptr) {
                    if (*ptr == '\n') {
                        if (++lc == 255)
                            break;
                    }
                    ptr++;
                }
                m_line_count = std::max(m_line_count, lc);
            }
        }
    };

    class ListViewSearchContextBase {
    public:
        virtual ~ListViewSearchContextBase() = default;

        virtual const char* get_item_text(size_t index) = 0;
    };

    class DefaultListViewSearchContext : public ListViewSearchContextBase {
    public:
        explicit DefaultListViewSearchContext(ListView* list_view) : m_list_view(list_view) {}

        const char* get_item_text(size_t index) override { return m_list_view->get_item_text(index, 0); }

    private:
        ListView* m_list_view{};
    };

    virtual void notify_on_selection_change(
        const pfc::bit_array& p_affected, const pfc::bit_array& p_status, notification_source_t p_notification_source)
    {
    }

    virtual void render_get_colour_data(ColourData& p_out);
    virtual std::unique_ptr<ListViewSearchContextBase> create_search_context()
    {
        return std::make_unique<DefaultListViewSearchContext>(this);
    }

    Item* get_item(size_t index) { return m_items[index].get_ptr(); }

    string_array& get_item_subitems(size_t index) { return m_items[index]->m_subitems; }

    size_t get_item_display_index(size_t index) { return m_items[index]->m_display_index; }

    [[nodiscard]] size_t get_item_display_group_count(size_t index) const
    {
        if (index == 0)
            return m_group_count;

        size_t counter = 0;
        size_t i = m_group_count;
        while (i && m_items[index]->m_groups[i - 1] != m_items[index - 1]->m_groups[i - 1]) {
            i--;
            counter++;
        }
        return counter;
    }

    void on_focus_change(size_t index_prev, size_t index_new);

    void set_group_level_indentation_enabled(bool group_level_indentation_enabled)
    {
        m_group_level_indentation_enabled = group_level_indentation_enabled;

        if (m_initialised) {
            update_column_sizes();
            build_header();
            refresh_item_positions();
        }
    }

    void set_group_level_indentation_amount(std::optional<int> group_level_indentation_amount)
    {
        m_group_level_indentation_amount = group_level_indentation_amount;

        if (m_initialised) {
            update_column_sizes();
            build_header();
            refresh_item_positions();
        }
    }

    int get_item_indentation() const;
    int get_indentation_step() const;
    int get_default_indentation_step() const;

    void set_group_info_area_size(int width, int height)
    {
        m_group_info_area_width = width;
        m_group_info_area_height = height;
        if (m_initialised) {
            update_column_sizes();
            update_header();
            refresh_item_positions();
        }
    }

    int get_group_info_area_width() { return get_show_group_info_area() ? m_group_info_area_width : 0; }

    int get_group_info_area_height() { return get_show_group_info_area() ? m_group_info_area_height : 0; }

    int get_group_info_area_total_width()
    {
        return get_show_group_info_area() ? m_group_info_area_width + get_indentation_step() : 0;
    }

    int get_group_info_area_total_height()
    {
        if (!get_show_group_info_area())
            return 0;

        if (!m_group_level_indentation_enabled)
            return m_group_info_area_height + get_default_indentation_step();

        return get_show_group_info_area() ? m_group_info_area_height + get_indentation_step() : 0;
    }

    void set_show_group_info_area(bool val)
    {
        bool b_old = get_show_group_info_area();
        m_show_group_info_area = val;
        bool b_new = get_show_group_info_area();
        if (m_initialised && (b_old != b_new)) {
            update_column_sizes();
            update_header();
            refresh_item_positions();
            on_size();
        }
    }

    std::optional<size_t> get_real_column_index(size_t header_item_index) const
    {
        if (!m_have_indent_column)
            return header_item_index;

        if (m_have_indent_column && header_item_index == 0) {
            return {};
        }

        return header_item_index - 1;
    }

    bool get_show_group_info_area() { return m_group_count ? m_show_group_info_area : false; }

    int get_total_indentation() { return get_item_indentation() + get_group_info_area_total_width(); }

    ColourData render_get_colour_data()
    {
        ColourData data;
        render_get_colour_data(data);
        return data;
    }

    COLORREF get_group_text_colour_default();
    bool get_group_text_colour_default(COLORREF& cr);

    void set_sort_column(std::optional<size_t> index, bool b_direction);

    void clear_sort_column() { set_sort_column({}, false); }

    void show_search_box(const char* label, bool b_focus = true);
    void close_search_box(bool b_notify = true);
    bool is_search_box_open();
    void focus_search_box();

private:
    struct ItemsFontConfig {
        wil::com_ptr<IDWriteTextFormat> text_format;
        LOGFONT log_font{};
    };

    virtual void on_first_show();

    virtual void notify_on_focus_item_change(size_t new_index) {}

    virtual void notify_on_initialisation() {} // set settings here
    virtual void notify_on_create() {} // populate list here
    virtual void notify_on_destroy() {}
    virtual bool notify_on_keyboard_keydown_filter(UINT msg, WPARAM wp, LPARAM lp) { return false; }
    virtual bool notify_on_contextmenu_header(const POINT& pt, const HDHITTESTINFO& ht) { return false; }
    virtual bool notify_on_contextmenu(const POINT& pt, bool from_keyboard) { return false; }
    virtual bool notify_on_timer(UINT_PTR timerid) { return false; }
    virtual void notify_on_time_change() {}
    virtual void notify_on_menu_select(WPARAM wp, LPARAM lp) {}
    virtual bool notify_on_middleclick(bool on_item, size_t index) { return false; }
    virtual bool notify_on_doubleleftclick_nowhere() { return false; }
    virtual void notify_sort_column(size_t index, bool b_descending, bool b_selection_only) {}
    virtual bool notify_on_keyboard_keydown_remove() { return false; }
    virtual bool notify_on_keyboard_keydown_undo() { return false; }
    virtual bool notify_on_keyboard_keydown_redo() { return false; }
    virtual bool notify_on_keyboard_keydown_cut() { return false; }

    virtual bool notify_on_keyboard_keydown_copy()
    {
        copy_selected_items_as_text();
        return true;
    }

    virtual bool notify_on_keyboard_keydown_paste() { return false; }
    virtual bool notify_on_keyboard_keydown_search() { return false; }
    virtual void notify_on_set_focus(HWND wnd_lost) {}
    virtual void notify_on_kill_focus(HWND wnd_receiving) {}
    virtual void notify_on_column_size_change(size_t index, int new_width) {}
    virtual void notify_on_group_info_area_size_change(int new_width) {}
    virtual void notify_on_header_rearrange(size_t index_from, size_t index_to) {}
    virtual void notify_update_item_data(size_t index) {}

    virtual size_t get_highlight_item() { return pfc_infinite; }
    virtual void execute_default_action(size_t index, size_t column, bool b_keyboard, bool b_ctrl) {}
    virtual void move_selection(int delta) {}
    virtual bool do_drag_drop(WPARAM wp) { return false; }

    virtual size_t storage_get_focus_item();
    virtual void storage_set_focus_item(size_t index);
    virtual void storage_get_selection_state(pfc::bit_array_var& out);
    virtual bool storage_set_selection_state(
        const pfc::bit_array& p_affected, const pfc::bit_array& p_status, pfc::bit_array_var* p_changed = nullptr);
    virtual bool storage_get_item_selected(size_t index);
    virtual size_t storage_get_selection_count(size_t max);

    virtual Item* storage_create_item() { return new Item; }
    virtual Group* storage_create_group() { return new Group; }

    virtual bool render_drag_image(LPSHDRAGIMAGE lpsdi);
    virtual wil::unique_hicon get_drag_image_icon() { return nullptr; }
    virtual size_t get_drag_item_count() /*const*/ { return get_selection_count(); }
    virtual bool should_show_drag_text(size_t selection_count) { return selection_count > 1; }
    virtual bool format_drag_text(size_t selection_count, pfc::string8& p_out);
    [[nodiscard]] virtual const char* get_drag_unit_singular() const { return "item"; }
    [[nodiscard]] virtual const char* get_drag_unit_plural() const { return "items"; }

    virtual void notify_on_search_box_contents_change(const char* p_str) {}
    virtual void notify_on_search_box_close() {}

    virtual bool notify_before_create_inline_edit(
        const pfc::list_base_const_t<size_t>& indices, size_t column, bool b_source_mouse)
    {
        return false;
    }

    virtual bool notify_create_inline_edit(const pfc::list_base_const_t<size_t>& indices, size_t column,
        pfc::string_base& p_test, size_t& p_flags, wil::com_ptr<IUnknown>& autocomplete_entries)
    {
        return true;
    }

    virtual void notify_save_inline_edit(const char* value) {}

    virtual void notify_exit_inline_edit() {}

    void update_vertical_scroll_info(bool redraw = true, std::optional<int> new_vertical_position = std::nullopt);
    void update_horizontal_scroll_info(bool redraw = true);

    [[nodiscard]] unsigned calculate_scroll_timer_speed() const;

    void create_timer_search();
    void destroy_timer_search();

    void process_navigation_keydown(WPARAM wp, bool alt_down, bool repeat);
    std::optional<LRESULT> on_wm_notify_header(LPNMHDR lpnm);
    bool on_wm_keydown(WPARAM wp, LPARAM lp);

    LRESULT on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);

    void render_items(HDC dc, const RECT& rc_update, int cx);
    void insert_items_in_internal_state(size_t index_start, size_t count, const InsertItem* items);
    void replace_items_in_internal_state(size_t index_start, size_t count, const InsertItem* items);
    void remove_item_in_internal_state(size_t index);
    void remove_items_in_internal_state(const pfc::bit_array& mask);
    void calculate_item_positions(size_t index_start = 0);

    static LRESULT WINAPI s_on_inline_edit_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
    LRESULT on_inline_edit_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);

    void create_inline_edit(const pfc::list_base_const_t<size_t>& indices, size_t column);
    void save_inline_edit();
    void exit_inline_edit();
    void set_inline_edit_window_theme() const;
    void set_inline_edit_rect() const;

    void on_search_string_change(WCHAR c);

    static LRESULT WINAPI s_on_search_edit_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
    LRESULT on_search_edit_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);
    void update_search_box_hot_status(const POINT& pt);

    void reset_columns();

    void set_window_theme() const;
    void reopen_themes();
    void close_themes();

    void refresh_items_font();
    void refresh_group_font();

    void create_header();
    void destroy_header();
    void set_header_window_theme() const;
    void build_header();
    void update_header();

    void create_tooltip(/*size_t index, size_t column, */ const char* str);
    void destroy_tooltip();
    void set_tooltip_window_theme() const;
    void calculate_tooltip_position(size_t item_index, size_t column_index, std::string_view text);
    std::optional<LRESULT> on_wm_notify_tooltip(LPNMHDR lpnm);
    void render_tooltip_text(HWND wnd, HDC dc, COLORREF colour) const;
    bool is_item_clipped(size_t index, size_t column);

    wil::unique_hfont m_items_font;
    wil::unique_hfont m_header_font;

    bool m_use_dark_mode{};
    wil::unique_htheme m_list_view_theme;
    wil::unique_htheme m_items_view_theme;
    wil::unique_htheme m_header_theme;
    wil::unique_htheme m_dd_theme;
    wil::unique_htheme m_tooltip_theme;

    HWND m_wnd_header{};

    HWND m_wnd_inline_edit{};
    WNDPROC m_proc_inline_edit{};
    WNDPROC m_proc_original_inline_edit{};
    std::unique_ptr<EventToken> m_inline_edit_mouse_hook;
    pfc::string8 m_inline_edit_initial_text;
    bool m_inline_edit_save{false};
    bool m_inline_edit_saving{false};
    bool m_timer_inline_edit{false};
    bool m_inline_edit_prevent{false};
    bool m_inline_edit_prevent_kill{false};
    size_t m_inline_edit_column{std::numeric_limits<size_t>::max()};
    pfc::list_t<size_t> m_inline_edit_indices;
    wil::com_ptr<IAutoComplete> m_inline_edit_autocomplete;
    wil::unique_hbrush m_edit_background_brush;
    COLORREF m_dark_edit_background_colour{};
    COLORREF m_dark_edit_text_colour{RGB(255, 255, 255)};

    std::optional<LOGFONT> m_items_log_font{};
    std::optional<LOGFONT> m_header_log_font{};
    direct_write::Context::Ptr m_direct_write_context;
    std::optional<direct_write::TextFormat> m_items_text_format;
    mutable std::optional<int> m_space_width;
    std::optional<direct_write::TextFormat> m_header_text_format;
    std::optional<direct_write::TextFormat> m_group_text_format;

    bool m_selecting{false};
    bool m_selecting_move{false};
    bool m_selecting_moved{false};
    bool m_dragging_rmb{false};
    size_t m_selecting_start{std::numeric_limits<size_t>::max()};
    size_t m_selecting_start_column{std::numeric_limits<size_t>::max()};
    HitTestResult m_lbutton_down_hittest;
    int m_scroll_position{0};
    int m_horizontal_scroll_position{0};
    bool m_scroll_bar_update_in_progress{};
    bool m_ensure_visible_suspended{};
    size_t m_group_count{0};
    int m_item_height{1};
    int m_group_height{1};
    size_t m_shift_start{std::numeric_limits<size_t>::max()};
    bool m_timer_scroll_up{false};
    bool m_timer_scroll_down{false};
    bool m_lbutton_down_ctrl{false};
    size_t m_insert_mark_index{std::numeric_limits<size_t>::max()};
    size_t m_highlight_item_index{std::numeric_limits<size_t>::max()};
    size_t m_highlight_selected_item_index{std::numeric_limits<size_t>::max()};
    POINT m_dragging_initial_point{0};
    POINT m_dragging_rmb_initial_point{0};
    bool m_shown{false};
    size_t m_focus_index{std::numeric_limits<size_t>::max()};
    bool m_autosize{false};
    bool m_initialised{false};
    bool m_always_show_focus{false};
    bool m_show_header{true};
    bool m_ignore_column_size_change_notification{false};
    int m_vertical_item_padding{};

    bool m_variable_height_items{false};

    bool m_prevent_wm_char_processing{false};
    bool m_timer_search{false};
    pfc::string8 m_search_string;

    bool m_show_tooltips{true};
    bool m_limit_tooltips_to_clipped_items{true};
    HWND m_wnd_tooltip{nullptr};
    RECT m_tooltip_text_rect{};
    RECT m_tooltip_placement_rect{};
    float m_tooltip_text_left_offset{};
    size_t m_tooltip_last_index{std::numeric_limits<size_t>::max()};
    size_t m_tooltip_last_column{std::numeric_limits<size_t>::max()};

    bool m_sorting_enabled{false};
    bool m_show_sort_indicators{true};
    std::optional<size_t> m_sort_column_index;
    bool m_sort_direction{false};
    EdgeStyle m_edge_style{edge_grey};
    bool m_sizing{false};

    SelectionMode m_selection_mode{SelectionMode::Multiple};
    bool m_alternate_selection{false};
    bool m_allow_header_rearrange{false};

    int m_group_info_area_width{0};
    int m_group_info_area_height{0};
    bool m_show_group_info_area{false};
    bool m_have_indent_column{false};

    HWND m_search_editbox{nullptr};
    WNDPROC m_proc_search_edit{nullptr};
    pfc::string8 m_search_label;
    bool m_search_box_hot{false};
    // HTHEME m_search_box_theme;
    // gdi_object_t<HBRUSH>::ptr_t m_search_box_hot_brush, m_search_box_nofocus_brush;
    bool m_is_high_contrast_active{};

    bool m_group_level_indentation_enabled{true};
    std::optional<int> m_group_level_indentation_amount;

    std::vector<t_item_ptr> m_items;
    std::vector<Column> m_columns;

    /**
     * \brief The underlying container window.
     */
    std::unique_ptr<ContainerWindow> m_container_window;
    std::unique_ptr<ContainerWindow> m_dummy_theme_window;
    std::unique_ptr<lv::RendererBase> m_renderer;
};

} // namespace uih
