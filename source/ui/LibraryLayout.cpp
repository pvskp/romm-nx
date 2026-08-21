#include "LibraryLayout.hpp"
#include "SidebarList.hpp"
#include "GameGrid.hpp"
#include "AlphabetBar.hpp"
#include "../navigation/NavigationManager.hpp"
#include "GlobalProgressBar.hpp"
#include "LibraryMenuModal.hpp"
#include "SyncModal.hpp"
#include "StatusBar.hpp"
#include "UninstallConfirmModal.hpp"
#include "../i18n/I18n.hpp"

namespace romm::ui {

    // Bottom hint with the bulk-selection state baked in: the X segment swaps
    // to "Unmark" and the "ZR Download marked" segment turns cream (the same
    // accent as the mark dots on the tiles) while any game is marked. Renders
    // from tr() every frame, so selection and language changes both pick up
    // immediately.
    class LibraryHint : public pu::ui::elm::Element {
    private:
        s32 x, y;
        std::weak_ptr<romm::navigation::NavigationManager> nav_mgr;
    public:
        LibraryHint(s32 hint_x, s32 hint_y, std::shared_ptr<romm::navigation::NavigationManager> nav)
            : Element(), x(hint_x), y(hint_y), nav_mgr(nav) {}

        s32 GetX() override { return x; }
        s32 GetY() override { return y; }
        s32 GetWidth() override { return 1920; }
        s32 GetHeight() override { return 40; }

        void OnRender(pu::ui::render::Renderer::Ref& drawer, const s32, const s32) override {
            auto nav = nav_mgr.lock();
            if (!nav) return;

            const bool marked = (nav->GetBulkSelectionCount() > 0);
            const pu::ui::Color base(190, 180, 225, 255);   // Light lavender
            const pu::ui::Color accent(230, 199, 167, 255); // Cream — selection accent

            s32 cx = x;
            auto draw = [&](const std::string& text, pu::ui::Color color) {
                pu::sdl2::Texture tex = pu::ui::render::RenderText("Ubuntu@30", text, color);
                if (!tex) return;
                drawer->RenderTexture(tex, cx, y);
                cx += pu::ui::render::GetTextureWidth(tex);
                pu::ui::render::DeleteTexture(tex);
            };

            draw(romm::i18n::tr("hint.library.seg.a"), base);
            // X swaps its label when games are marked, but stays quiet —
            // the cream spotlight belongs to the ZR action alone.
            draw(romm::i18n::tr(marked ? "hint.library.unmark" : "hint.library.mark"), base);
            draw(romm::i18n::tr("hint.library.seg.b"), base);
            draw(romm::i18n::tr("hint.library.download_marked"), marked ? accent : base);
            draw(romm::i18n::tr("hint.library.seg.c"), base);
        }

        void OnInput(const u64, const u64, const u64, const pu::ui::TouchPoint) override {}
    };

    LibraryLayout::LibraryLayout(std::shared_ptr<romm::navigation::NavigationManager> nav)
        : Layout::Layout(), nav_mgr(nav) {

        // Background color: Web Dark Slate (#101216)
        this->SetBackgroundColor(pu::ui::Color(16, 18, 22, 255));

        // Create Title text block (Orbitron Black, Very light text #EDE5FB).
        // "ROMM" is the product name — not translated in any language.
        title_text = pu::ui::elm::TextBlock::New(60, 30, "ROMM");
        title_text->SetFont("Orbitron@45");
        title_text->SetColor(pu::ui::Color(237, 229, 251, 255));
        this->Add(title_text);

        // Wi-Fi / battery / storage cluster (own polling throttle, see StatusBar)
        status_bar = StatusBar::New(0, 0, 1920, 1080);
        this->Add(status_bar);

        // Create hint text block (Ubuntu, Light lavender #BEB4E1). Rendered
        // by LibraryHint so the ZR segment can light up when games are marked.
        this->Add(std::make_shared<LibraryHint>(60, 1080 - 65, nav));

        // Global Progress Bar (Top left, free space)
        auto global_progress = romm::ui::GlobalProgressBar::New(380 + 16, 14, 460, 56, nav);
        this->Add(global_progress);

        // Create sidebar, alphabet bar, and grid components
        // Sidebar takes width=380, height=880 (from y=100 to y=980)
        sidebar = SidebarList::New(0, 100, 380, 880, nav);
        this->Add(sidebar);

        // Alphabet bar takes the top of the grid area
        alphabet_bar = AlphabetBar::New(380 + 30, 100, 1920 - 380 - 60, 60, nav);
        this->Add(alphabet_bar);

        // Grid takes the remaining space below the alphabet bar
        grid = GameGrid::New(380, 180, 1920 - 380, 800, nav);
        this->Add(grid);

        // Uninstall confirmation. Needed here because Detail view mode's panel
        // can trigger an uninstall without ever leaving the Library screen —
        // NavigationManager gates all input on uninstall_modal.active, so a
        // layout that can raise the modal but not draw it would soft-lock.
        this->Add(romm::ui::UninstallConfirmModal::New(nav));

        // Y-Menu overlay — added last so it renders on top of everything else
        library_menu_modal = LibraryMenuModal::New(nav);
        this->Add(library_menu_modal);

        // Sync overlay: the platform-wide sync can be started from the Y-Menu
        // without ever entering the Detail screen, so the modal must exist
        // here too.
        this->Add(romm::ui::SyncModal::New(nav));
    }

    LibraryLayout::~LibraryLayout() {}

    void LibraryLayout::RefreshTranslations() {
        // The bottom hint is rendered from tr() every frame by LibraryHint, so
        // it needs no rebuild here.
        // The sidebar's status cards and the grid's status/info strips are
        // pre-rendered textures, so they need an explicit rebuild; everything
        // else on this screen is drawn from tr() each frame.
        if (sidebar) sidebar->RefreshTranslations();
        if (grid) grid->RefreshTranslations();
    }

    void LibraryLayout::OnSelectionUpdated() {
        auto nav = nav_mgr.lock();
        if (nav && grid) {
            if (sidebar) {
                sidebar->Refresh();
            }
            if (nav->ShowAlphabetFilter()) {
                grid->SetY(180);
                grid->SetHeight(800);
            } else {
                grid->SetY(100);
                grid->SetHeight(880);
            }
            grid->OnSelectionUpdated();
        }
    }

}
