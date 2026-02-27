#ifndef SCREEN_H
#define SCREEN_H

// ============================================================================
// Screen Base Class
// ============================================================================
// Pure virtual base class for all display screens.
// Each screen manages its own LVGL widgets and lifecycle.
//
// Lifecycle:
// 1. create()  - Build LVGL widgets (called once)
// 2. show()    - Make screen visible (lv_scr_load)
// 3. update()  - Refresh data (called every loop while active)
// 4. hide()    - Hide screen (optional cleanup)
// 5. destroy() - Clean up widgets (called before deletion)

class Screen {
public:
		virtual ~Screen() {}
		
		// Build LVGL widgets for this screen
		virtual void create() = 0;
		
		// Clean up LVGL widgets
		virtual void destroy() = 0;
		
		// Make this screen visible
		virtual void show() = 0;
		
		// Hide this screen (called before navigating away)
		virtual void hide() = 0;
		
		// Update screen data (called every loop while active)
		// Read from stored pointers (thread-safe: main loop only)
		virtual void update() = 0;
};

#endif // SCREEN_H
