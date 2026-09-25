#include <fcntl.h>
#include <gio/gio.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "../../application.h"
#include "../../foundation/dispatcher.h"
#include "../../menu.h"
#include "../../window_manager.h"

namespace nativeapi {

class Application::Impl {
 public:
  Impl(Application* app) : app_(app), gtk_app_(nullptr), lock_file_handle_(-1) {}
  ~Impl() = default;

  bool Initialize() {
    // Initialize GTK
    gtk_init(nullptr, nullptr);

    // Create GTK application with default ID. G_APPLICATION_DEFAULT_FLAGS is
    // GLib 2.74+; older distributions (and manylinux wheels) spell it _NONE.
#if GLIB_CHECK_VERSION(2, 74, 0)
    constexpr GApplicationFlags kFlags = G_APPLICATION_DEFAULT_FLAGS;
#else
    constexpr GApplicationFlags kFlags = G_APPLICATION_FLAGS_NONE;
#endif
    gtk_app_ = gtk_application_new("com.nativeapi.application", kFlags);

    if (!gtk_app_) {
      return false;
    }

    // Set default application name. GtkApplication has no "application-name"
    // property; the name is a process-wide GLib setting.
    g_set_application_name("NativeAPI Application");

    // Connect to GTK application signals
    g_signal_connect(gtk_app_, "startup", G_CALLBACK(OnStartup), this);
    g_signal_connect(gtk_app_, "activate", G_CALLBACK(OnActivate), this);

    return true;
  }

  int Run() {
    // A GApplication exits its main loop as soon as nothing keeps it alive, and
    // a windowless application has nothing. Hold it until Quit().
    if (!Register()) {
      return -1;
    }
    g_application_hold(G_APPLICATION(gtk_app_));

    // Run the GTK main loop
    int status = g_application_run(G_APPLICATION(gtk_app_), 0, nullptr);

    // g_application_quit() carries no status; Quit() recorded it.
    return status != 0 ? status : app_->exit_code_;
  }

  int Run(std::shared_ptr<Window> window) {
    if (!window) {
      return -1;
    }

    // Set the window as primary window
    app_->SetPrimaryWindow(window);

    // Windows are created as plain GtkWindows, so the GApplication does not know
    // about them and would return from its main loop immediately. Hand it the
    // window — only possible once the application is registered.
    if (!Register()) {
      return -1;
    }
    GtkWidget* widget = static_cast<GtkWidget*>(window->GetNativeObject());
    if (widget && GTK_IS_WINDOW(widget)) {
      gtk_application_add_window(GTK_APPLICATION(gtk_app_), GTK_WINDOW(widget));
    } else {
      // No window to keep it alive: fall back to an explicit hold.
      g_application_hold(G_APPLICATION(gtk_app_));
    }

    // Show the window
    window->Show();
    window->Focus();

    // Run the GTK main loop
    int status = g_application_run(G_APPLICATION(gtk_app_), 0, nullptr);

    // g_application_quit() carries no status; Quit() recorded it.
    return status != 0 ? status : app_->exit_code_;
  }

  void Quit(int exit_code) { g_application_quit(G_APPLICATION(gtk_app_)); }

  // gtk_application_add_window() and g_application_hold() only take effect on a
  // registered application, and g_application_run() registers too late for that.
  bool Register() {
    GError* error = nullptr;
    if (g_application_register(G_APPLICATION(gtk_app_), nullptr, &error)) {
      return true;
    }
    std::cerr << "Failed to register application: " << (error ? error->message : "unknown error")
              << std::endl;
    g_clear_error(&error);
    return false;
  }

  bool SetIcon(const std::string& icon_path) {
    if (icon_path.empty()) {
      return false;
    }

    // Load icon from file
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file(icon_path.c_str(), nullptr);
    if (!pixbuf) {
      return false;
    }

    // Set application icon
    gtk_window_set_default_icon(pixbuf);

    g_object_unref(pixbuf);
    return true;
  }

  bool SetDockIconVisible(bool visible) {
    // Linux doesn't have a dock in the same way as macOS
    // This is a no-op for now
    return true;
  }

  bool SetProgressBar(double progress) {
    launcher_progress_visible_ = progress >= 0;
    // LauncherEntry has no indeterminate state; show "busy" as full.
    launcher_progress_ = progress > 1 ? 1.0 : (progress < 0 ? 0.0 : progress);
    return EmitLauncherEntryUpdate();
  }

  bool SetBadgeLabel(const std::string& label) {
    if (label.empty()) {
      launcher_count_visible_ = false;
      launcher_count_ = 0;
      return EmitLauncherEntryUpdate();
    }
    char* end = nullptr;
    long long count = std::strtoll(label.c_str(), &end, 10);
    if (end == label.c_str() || *end != '\0') {
      return false;  // LauncherEntry badges are numeric only.
    }
    launcher_count_visible_ = true;
    launcher_count_ = count;
    return EmitLauncherEntryUpdate();
  }

  bool SetBrightness(Brightness brightness) {
    GtkSettings* settings = gtk_settings_get_default();
    if (!settings) {
      return false;
    }

    if (brightness == Brightness::System) {
      gtk_settings_reset_property(settings, "gtk-application-prefer-dark-theme");
      if (!original_theme_name_.empty()) {
        g_object_set(settings, "gtk-theme-name", original_theme_name_.c_str(), nullptr);
        original_theme_name_.clear();
      }
      return true;
    }

    const gboolean dark = brightness == Brightness::Dark;
    g_object_set(settings, "gtk-application-prefer-dark-theme", dark, nullptr);

    if (!dark) {
      // prefer-dark-theme=false is not enough when the active theme is itself a
      // dark variant such as "Yaru-dark"; switch to the light variant and
      // remember the original so Brightness::System can put it back.
      gchar* theme_name = nullptr;
      g_object_get(settings, "gtk-theme-name", &theme_name, nullptr);
      if (theme_name && g_str_has_suffix(theme_name, "-dark")) {
        if (original_theme_name_.empty()) {
          original_theme_name_ = theme_name;
        }
        gchar* light_theme_name = g_strndup(theme_name, strlen(theme_name) - 5);
        g_object_set(settings, "gtk-theme-name", light_theme_name, nullptr);
        g_free(light_theme_name);
      }
      g_free(theme_name);
    }
    return true;
  }

  bool SetMenuBar(std::shared_ptr<Menu> menu) {
    if (!menu) {
      return false;
    }

    // Get the native menu handle
    GtkWidget* gtk_menu = static_cast<GtkWidget*>(menu->GetNativeObject());
    if (!gtk_menu) {
      return false;
    }

    // Note: gtk_application_set_app_menu expects GMenuModel, but our Menu
    // class uses legacy GtkMenu widgets. Setting application menu bar is not
    // supported with legacy menus in GTK3. Users should add menu bars directly
    // to their windows instead.
    // TODO: Consider implementing GMenuModel-based menus in the future.

    return false;  // Not supported with legacy GtkMenu
  }

  void CleanupEventMonitoring() {
    // Clean up Linux-specific event monitoring
    if (lock_file_handle_ != -1) {
      close(lock_file_handle_);
      lock_file_handle_ = -1;
    }

    if (gtk_app_) {
      g_object_unref(gtk_app_);
      gtk_app_ = nullptr;
    }
  }

 private:
  Application* app_;
  GtkApplication* gtk_app_;
  int lock_file_handle_;
  double launcher_progress_ = 0.0;
  bool launcher_progress_visible_ = false;
  long long launcher_count_ = 0;
  bool launcher_count_visible_ = false;
  std::string original_theme_name_;

  // Broadcasts the com.canonical.Unity.LauncherEntry "Update" signal that
  // Unity, Plasma, elementary and Dash to Dock listen for. The entry is keyed
  // on the desktop file, which is assumed to be named after the program.
  bool EmitLauncherEntryUpdate() {
    const gchar* program_name = g_get_prgname();
    if (!program_name) {
      return false;
    }
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if (!bus) {
      return false;
    }

    gchar* app_uri = g_strdup_printf("application://%s.desktop", program_name);
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&builder, "{sv}", "progress", g_variant_new_double(launcher_progress_));
    g_variant_builder_add(&builder, "{sv}", "progress-visible",
                          g_variant_new_boolean(launcher_progress_visible_));
    g_variant_builder_add(&builder, "{sv}", "count", g_variant_new_int64(launcher_count_));
    g_variant_builder_add(&builder, "{sv}", "count-visible",
                          g_variant_new_boolean(launcher_count_visible_));
    GVariant* parameters = g_variant_new("(sa{sv})", app_uri, &builder);

    gboolean ok = g_dbus_connection_emit_signal(bus, nullptr, "/", "com.canonical.Unity.LauncherEntry",
                                                "Update", parameters, nullptr);
    g_free(app_uri);
    g_object_unref(bus);
    return ok;
  }

  static void OnStartup(GApplication* app, gpointer user_data) {
    Impl* impl = static_cast<Impl*>(user_data);

    // Emit application started event
    ApplicationStartedEvent event;
    impl->app_->Emit(event);
  }

  static void OnActivate(GApplication* app, gpointer user_data) {
    Impl* impl = static_cast<Impl*>(user_data);

    // Emit application activated event
    ApplicationActivatedEvent event;
    impl->app_->Emit(event);
  }
};

Application::Application()
    : initialized_(true), running_(false), exit_code_(0), pimpl_(std::make_unique<Impl>(this)) {
  // Perform platform-specific initialization automatically
  pimpl_->Initialize();

  // Emit application started event
  Emit<ApplicationStartedEvent>();
}

Application::~Application() {
  // Clean up platform-specific event monitoring
  pimpl_->CleanupEventMonitoring();
}

int Application::Run() {
  running_ = true;
  exit_code_ = 0;

  // Start the platform-specific main event loop
  int result = pimpl_->Run();

  running_ = false;

  // Emit exit event
  Emit<ApplicationExitingEvent>(result);

  return result;
}

int Application::Run(std::shared_ptr<Window> window) {
  if (!window) {
    return -1;  // Invalid window
  }

  running_ = true;
  exit_code_ = 0;

  // Start the platform-specific main event loop with window
  int result = pimpl_->Run(window);

  running_ = false;

  // Emit exit event
  Emit<ApplicationExitingEvent>(result);

  return result;
}

void Application::Quit(int exit_code) {
  // The loop, and every listener, lives on the main thread; a quit requested
  // from another thread is carried over there.
  if (!IsMainThread() && RunOnMainThread([this, exit_code] { Quit(exit_code); })) {
    return;
  }

  exit_code_ = exit_code;

  // A QuitRequested listener may itself call Quit(): record its exit code and
  // let the outer call finish, instead of recursing.
  static bool announcing = false;
  if (announcing) {
    return;
  }
  announcing = true;
  Emit<ApplicationQuitRequestedEvent>();
  announcing = false;

  // Request platform-specific quit, with the last exit code asked for
  pimpl_->Quit(exit_code_);
}

bool Application::IsRunning() const {
  return running_;
}

bool Application::IsSingleInstance() const {
  return false;
}

bool Application::SetIcon(const std::string& icon_path) {
  return pimpl_->SetIcon(icon_path);
}

bool Application::SetDockIconVisible(bool visible) {
  return pimpl_->SetDockIconVisible(visible);
}

bool Application::SetProgressBar(double progress) {
  return pimpl_->SetProgressBar(progress);
}

bool Application::SetBadgeLabel(const std::string& label) {
  return pimpl_->SetBadgeLabel(label);
}

bool Application::SetBrightness(Brightness brightness) {
  return pimpl_->SetBrightness(brightness);
}

bool Application::SetMenuBar(std::shared_ptr<Menu> menu) {
  return pimpl_->SetMenuBar(menu);
}

std::shared_ptr<Window> Application::GetPrimaryWindow() const {
  return primary_window_;
}

void Application::SetPrimaryWindow(std::shared_ptr<Window> window) {
  primary_window_ = window;
}

std::vector<std::shared_ptr<Window>> Application::GetAllWindows() const {
  auto& window_manager = WindowManager::GetInstance();
  return window_manager.GetAll();
}

}  // namespace nativeapi
