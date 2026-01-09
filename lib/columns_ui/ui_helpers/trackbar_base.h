#pragma once

namespace uih {

using TrackbarString = std::basic_string<TCHAR>;

/**
 * Class for host of trackbar control to (optionally) implement to receive callbacks.
 * \see TrackbarBase::set_callback
 */
class NOVTABLE TrackbarCallback {
public:
    /**
     * Called when thumb position changes.
     *
     * \param [in]    pos        Contains new position of track bar thumb
     * \param [in]    b_tracking Specifies that the user is dragging the thumb if true
     *
     * \see TrackbarBase::set_range
     */
    virtual void on_position_change(unsigned pos, bool b_tracking) {}

    /**
     * Called to retrieve tooltip text when tracking
     *
     * \param [in]     pos       Contains position of track bar thumb
     * \param [out]    p_out     Receives tooltip text, utf-8 encoded
     *
     * \see TrackbarBase::set_range, TrackbarBase::set_show_tooltips
     */
    virtual void get_tooltip_text(unsigned pos, TrackbarString& p_out) {}

    /**
     * Called when the escape or return key changes state whilst the
     * track bar has the keyboard focus
     *
     * \param [in]    wp         Specifies the virtual-key code of the nonsystem key
     * \param [in]    lp         Specifies the repeat count, scan code, extended-key flag,
     *                           context code, previous key-state flag, and transition-state flag
     *                           (see MSDN)
     *
     * \return                    whether you processed the key state change
     */
    virtual bool on_key(WPARAM wp, LPARAM lp) { return false; }
};

/**
 * Track bar base class.
 *
 * Implemented by trackbar clients, used by host.
 */
class TrackbarBase : MessageHook {
public:
    /**
     * \brief               Creates a new track bar window
     * \param wnd_parent    Handle of the parent window
     * \param window_pos    Initial window position
     * \return              Window handle of the created track bar
     */
    HWND create(HWND wnd_parent, WindowPosition window_pos = {})
    {
        return m_container_window->create(wnd_parent, window_pos);
    }

    /**
     * \brief   Destroys the track bar window.
     */
    void destroy() { m_container_window->destroy(); }

    /**
     * \brief   Gets the track bar window handle.
     * \return  Track bar window handle
     */
    HWND get_wnd() const { return m_container_window->get_wnd(); }

    /**
     * Sets position of trackbar thumb
     *
     * \param [in]    pos            Specifies new position of track bar thumb
     *
     * \note    No range checking is done!
     *
     * \see TrackbarBase::set_range
     */
    void set_position(unsigned pos);

    /**
     * Retrieves position of trackbar thumb
     *
     * \return                    Position of track bar thumb
     *
     * \see TrackbarBase::set_range
     */
    unsigned get_position() const;

    /**
     * Sets range of track bar control
     *
     * \param [in]    val            New maximum position of thumb
     *
     * \note    Minimum position of thumb is zero
     */
    void set_range(unsigned val);

    /**
     * Retrieves range of track bar control
     *
     * \return                    Maximum position of thumb
     *
     * \note    Minimum position of thumb is zero
     */
    unsigned get_range() const;

    /**
     * Sets scroll step for scrolling a single line
     *
     * \param [in]    u_val        New scroll step
     */
    void set_scroll_step(unsigned u_val);

    /**
     * Retrieves scroll step for scrolling a single line
     *
     * \return                    Scroll step
     */
    unsigned get_scroll_step() const;

    /**
     * Sets orientation of track bar control
     *
     * \param [in]    b_vertical    New vertically orientated state for track bar
     *
     * \note    Not vertically orientated implies horizontally orientated.
     */
    void set_orientation(bool b_vertical);

    /**
     * Retrieves orientation of track bar control
     *
     * \return                       Vertically orientated track bar state
     *
     * \note    Not vertically orientated implies horizontally orientated.
     */
    bool get_orientation() const;

    /**
     * Sets take focus on mouse button down state
     *
     * \param [in]    b_auto_focus   New auto focus state
     *
     * \note    Default state is false
     */
    void set_auto_focus(bool b_auto_focus);

    /**
     * Gets take focus on mouse button down state
     *
     * \return                       Auto focus state
     *
     * \note    Default state is false
     */
    bool get_auto_focus() const;

    /**
     * Sets direction of track bar control
     *
     * \param [in]    b_reversed    New reversed direction state for track bar
     *
     * \note    Default direction is rightwards/upwards increasing.
     */
    void set_direction(bool b_reversed);

    /**
     * Sets direction of mouse wheel scrolling
     *
     * \param [in]    b_reversed    New reversed direction state for track bar
     *
     * \note    Default direction is rightwards/upwards increasing.
     */
    void set_mouse_wheel_direction(bool b_reversed);

    /**
     * Retrieves direction of track bar control
     *
     * \return                    Reversed direction state track bar state
     *
     * \note    Default direction is rightwards/upwards increasing.
     */
    bool get_direction() const;

    /**
     * Sets enabled state of track bar
     *
     * \param [in]    b_enabled    New enabled state
     *
     * \note The window is enabled by default
     */
    void set_enabled(bool b_enabled);

    /**
     * Retrieves enabled state of track bar
     *
     * \return                    Enabled state
     */
    bool get_enabled() const;

    /**
     * Retrieves hot state of track bar thumb
     *
     * \return                    Hot state. The thumb is hot when the mouse is over it
     */
    bool get_hot() const;

    /**
     * Retrieves tracking state of track bar thumb
     *
     * \return                    Tracking state. The thumb is tracking when it is being dragged
     */
    bool get_tracking() const;

    /**
     * Retrieves thumb rect for given position and range
     *
     * \param [in]    pos            Position of thumb
     * \param [in]    range          Maximum position of thumb
     * \param [out]    rc            Receives thumb rect
     *
     * \note    Minimum position of thumb is zero
     * \note Track bar implementations must implement this function
     */
    virtual void get_thumb_rect(unsigned pos, unsigned range, RECT* rc) const = 0;

    /**
     * Retrieves thumb rect for current position and range (helper)
     *
     * \param [out]    rc            Receives thumb area in client co-ordinates
     */
    void get_thumb_rect(RECT* rc) const;

    /**
     * Retrieves channel rect
     *
     * \param [out]    rc            Receives channel area in client co-ordinates
     *
     * \note Track bar implementations must implement this function
     */
    virtual void get_channel_rect(RECT* rc) const = 0;

    /**
     * Draws track bar thumb
     *
     * \param [in]    dc            Handle to device context for the drawing operation
     * \param [in]    rc            Rectangle specifying the thumb area in client co-ordinates
     *
     * \note Track bar implementations must implement this function
     */
    virtual void draw_thumb(HDC dc, const RECT* rc) const = 0;

    /**
     * Draws track bar channel
     *
     * \param [in]    dc            Handle to device context for the drawing operation
     * \param [in]    rc            Rectangle specifying the channel area in client co-ordinates
     *
     * \note Track bar implementations must implement this function
     */
    virtual void draw_channel(HDC dc, const RECT* rc) const = 0;

    /**
     * Draws track bar background
     *
     * \param [in]    dc            Handle to device context for the drawing operation
     * \param [in]    rc            Rectangle specifying the background area in client co-ordinates
     *                              This is equal to the client area.
     *
     * \note Track bar implementations must implement this function
     */
    virtual void draw_background(HDC dc, const RECT* rc) const = 0;

    /**
     * Sets host callback interface
     *
     * \param [in]    p_host        pointer to host interface
     *
     * \note The pointer must be valid until the track bar is destroyed, or until a
     * subsequent call to this function
     */
    void set_callback(TrackbarCallback* p_host);

    /**
     * Sets whether tooltips are shown
     *
     * \param [in]    b_show        specifies whether to show tooltips while tracking
     *
     * \note Tooltips are disabled by default
     */
    void set_show_tooltips(bool b_show);

    /**
     * \brief Set whether tooltips are dark themed.
     */
    void set_are_tooltips_dark(bool are_tooltips_dark);

    /**
     * Default constructor for TrackbarBase class
     */
    TrackbarBase()
    {
        auto window_config = uih::ContainerWindowConfig{L"ui_extension_track_bar"};
        m_container_window = std::make_unique<uih::ContainerWindow>(
            window_config, [this](auto&&... args) { return on_message(std::forward<decltype(args)>(args)...); });
    }

    virtual ~TrackbarBase() = default;

protected:
    /**
     * Retreives a handle to the theme context to be used for calling uxtheme APIs
     *
     * \return                    Handle to the theme.
     * \par
     *                            May be NULL in case theming is not active.
     *                            In this case, use non-themed rendering.
     * \par
     *                            The returned handle must not be released!
     */
    HTHEME get_theme_handle() const;

private:
    /**
     * Message handler for track bar.
     *
     * \param [in]    wnd           Specifies window receiving the message
     * \param [in]    msg           Specifies message code
     * \param [in]    wp            Specifies message-specific WPARAM code
     * \param [in]    lp            Specifies message-specific LPARAM code
     *
     * \return                      Message-specific value
     */
    LRESULT on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);

    /**
     * \brief Handles hooked keyboard events.
     *
     * Used to capture Esc presses when dragging the thumb outside of the
     * window boundary.
     */
    bool on_hooked_message(uih::MessageHookType p_type, int code, WPARAM wp, LPARAM lp) override;

    /**
     * Used internally by the track bar.\n
     * Sets position of the thumb and re-renders regions invalided as a result.
     *
     * \param [in]    pos            Position of the thumb, within the specified range.
     *
     * \see set_range
     */
    void set_position_internal(unsigned pos);

    /**
     * Used internally by the track bar.\n
     * Used to update the hot status of the thumb when the mouse cursor moves.
     *
     * \param [in]    pt            New position of the mouse pointer, in client co-ordinates.
     */
    void update_hot_status(POINT pt);

    /**
     * Used internally by the track bar.\n
     * Used to calculate the position at a given point.
     *
     * \param [in]    pt            Client co-ordinate to test.
     * \return                      Position at pt specified.
     */
    unsigned calculate_position_from_point(const POINT& pt) const;

    /**
     * Used internally by the track bar.\n
     * Creates a tracking tooltip.
     *
     * \param [in]    text          Text to display in the tooltip.
     * \param [in]    pt            Position of the mouse pointer, in screen co-ordinates.
     */
    BOOL create_tooltip(const TCHAR* text, POINT pt);

    /**
     * \brief Set the window theme of the tooltip.
     */
    void set_tooltip_theme() const;

    /**
     * Used internally by the track bar.\n
     * Destroys the tracking tooltip.
     */
    void destroy_tooltip();

    /**
     * Used internally by the track bar.\n
     * Updates position and text of tooltip.
     *
     * \param [in]    pt            Position of the mouse pointer, in screen co-ordinates.
     * \param [in]    text          Text to display in the tooltip.
     */
    BOOL update_tooltip(POINT pt, const TCHAR* text);

    /** Handle to the theme used for theme rendering. */
    HTHEME m_theme{nullptr};

    /**
     * Current position of the thumb.
     */
    unsigned m_position{0};
    /**
     * Maximum position of the thumb.
     */
    unsigned m_range{0};

    int m_cursor_height{};

    /**
     * Hot state of the thumb.
     */
    bool m_thumb_hot{false};

    /**
     * Tracking state of the thumb.
     */
    bool m_dragging{false};

    /** Orientation of the thumb. */
    bool m_vertical{false};

    /** Reversed state of the thumb. */
    bool m_reversed{false};

    /** Reversed state of mouse wheel scrolling. */
    bool m_mouse_wheel_reversed{false};

    /** Automatically take focus on mouse button down state. */
    bool m_auto_focus{false};

    /** Stores hook registration state. */
    bool m_hook_registered{false};

    /** Scroll step for scrolling a single line */
    unsigned m_step{1};

    /** Current display position of the thumb. Used when tracking. */
    unsigned m_display_position{0};

    /**
     * Show tooltips state.
     */
    bool m_show_tooltips{false};

    bool m_are_tooltips_dark{};

    /**
     * Window focus was obtained from.
     */
    HWND m_wnd_prev{nullptr};

    /**
     * Handle to tooltip window.
     */
    HWND m_wnd_tooltip{nullptr};

    class t_last_mousemove {
    public:
        bool m_valid{false};
        WPARAM m_wp{NULL};
        LPARAM m_lp{NULL};
    } m_last_mousemove;

    /**
     * Pointer to host interface.
     */
    TrackbarCallback* m_host;

    /**
     * \brief The underlying container window.
     */
    std::unique_ptr<uih::ContainerWindow> m_container_window;
};

} // namespace uih
