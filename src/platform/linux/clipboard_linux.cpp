#include <gtk/gtk.h>

#include <algorithm>
#include <cstring>

#include "../../clipboard_impl.h"

namespace nativeapi {
namespace {
constexpr unsigned Bit(ClipboardReadFormat format) {
  return 1u << static_cast<unsigned>(format);
}
GtkClipboard* Board() {
  GdkDisplay* display = gdk_display_get_default();
  if (!display && gtk_init_check(nullptr, nullptr))
    display = gdk_display_get_default();
  return display ? gtk_clipboard_get_for_display(display, GDK_SELECTION_CLIPBOARD) : nullptr;
}

struct ReadState : std::enable_shared_from_this<ReadState> {
  GtkClipboard* board;
  unsigned mask;
  std::function<void(bool, ClipboardData)> callback;
  ClipboardData data;
  std::vector<ClipboardReadFormat> formats;
  GdkAtom html = GDK_NONE;
  GdkAtom text = GDK_NONE;
  size_t next = 0;
  gulong owner_signal = 0;
  guint timeout = 0;
  bool changed = false;
  bool done = false;

  explicit ReadState(GtkClipboard* board, unsigned mask) : board(board), mask(mask) {
    g_object_ref(board);
  }
  ~ReadState() { g_object_unref(board); }
  using Ref = std::shared_ptr<ReadState>;
  gpointer Retain() { return new Ref(shared_from_this()); }
  static Ref Take(gpointer value) {
    std::unique_ptr<Ref> ref(static_cast<Ref*>(value));
    return *ref;
  }
  void Finish(bool ok) {
    if (done)
      return;
    done = true;
    if (owner_signal) {
      g_signal_handler_disconnect(board, owner_signal);
      owner_signal = 0;
    }
    if (timeout) {
      g_source_remove(timeout);
      timeout = 0;
    }
    ok = ok && !changed && clipboard_internal::ValidData(data);
    if (callback) {
      auto complete = std::move(callback);
      complete(ok, ok ? std::move(data) : ClipboardData{});
    }
  }
  void Watch() {
    owner_signal = g_signal_connect(
        board, "owner-change", G_CALLBACK(+[](GtkClipboard*, GdkEventOwnerChange*, gpointer ptr) {
          static_cast<ReadState*>(ptr)->changed = true;
        }),
        this);
  }
  void Start() {
    timeout = g_timeout_add_full(
        G_PRIORITY_DEFAULT, 5000,
        +[](gpointer value) -> gboolean {
          auto self = *static_cast<Ref*>(value);
          self->timeout = 0;
          self->Finish(false);
          return G_SOURCE_REMOVE;
        },
        Retain(), +[](gpointer value) { delete static_cast<Ref*>(value); });
    gtk_clipboard_request_targets(
        board,
        +[](GtkClipboard*, GdkAtom* targets, gint count, gpointer ptr) {
          auto self = Take(ptr);
          if (self->done)
            return;
          // No owner is a successful empty clipboard. GTK reports it as no targets.
          if (count > 0 && targets) {
            if (gtk_targets_include_text(targets, count))
              self->formats.push_back(ClipboardReadFormat::Text);
            for (gint i = 0; i < count; ++i) {
              gchar* name = gdk_atom_name(targets[i]);
              if (name && (std::strcmp(name, "UTF8_STRING") == 0 ||
                           std::strcmp(name, "text/plain;charset=utf-8") == 0 ||
                           std::strcmp(name, "text/plain") == 0)) {
                if (self->text == GDK_NONE || std::strcmp(name, "UTF8_STRING") == 0)
                  self->text = targets[i];
              }
              if (name && (std::strcmp(name, "text/html") == 0 ||
                           std::strcmp(name, "text/html;charset=utf-8") == 0))
                self->html = targets[i];
              g_free(name);
            }
            if (self->html != GDK_NONE)
              self->formats.push_back(ClipboardReadFormat::Html);
            if (gtk_targets_include_image(targets, count, FALSE))
              self->formats.push_back(ClipboardReadFormat::Image);
            if (gtk_targets_include_uri(targets, count))
              self->formats.push_back(ClipboardReadFormat::FilePaths);
          }
          self->Next();
        },
        Retain());
  }
  void Next() {
    if (done)
      return;
    if (changed) {
      Finish(false);
      return;
    }
    while (next < formats.size() && !(mask & Bit(formats[next])))
      ++next;
    if (next == formats.size()) {
      Finish(true);
      return;
    }
    const auto format = formats[next++];
    if (format == ClipboardReadFormat::Text && text != GDK_NONE) {
      gtk_clipboard_request_contents(
          board, text,
          +[](GtkClipboard*, GtkSelectionData* selection, gpointer ptr) {
            auto self = Take(ptr);
            if (self->done)
              return;
            gint size = gtk_selection_data_get_length(selection);
            const auto* bytes = gtk_selection_data_get_data(selection);
            if (size < 0 || (size && !bytes)) {
              self->Finish(false);
              return;
            }
            if (size && bytes[size - 1] == 0)
              --size;
            self->data.text =
                size ? std::string(reinterpret_cast<const char*>(bytes), size) : std::string{};
            self->Next();
          },
          Retain());
    } else if (format == ClipboardReadFormat::Text) {
      gtk_clipboard_request_text(
          board,
          +[](GtkClipboard*, const gchar* text, gpointer ptr) {
            auto self = Take(ptr);
            if (self->done)
              return;
            if (!text) {
              self->Finish(false);
              return;
            }
            self->data.text = text;
            self->Next();
          },
          Retain());
    } else if (format == ClipboardReadFormat::Html) {
      gtk_clipboard_request_contents(
          board, html,
          +[](GtkClipboard*, GtkSelectionData* selection, gpointer ptr) {
            auto self = Take(ptr);
            if (self->done)
              return;
            gint size = gtk_selection_data_get_length(selection);
            if (size < 0) {
              self->Finish(false);
              return;
            }
            const auto* bytes = gtk_selection_data_get_data(selection);
            // Some providers include the trailing terminator in their selection.
            if (size && bytes[size - 1] == 0)
              --size;
            self->data.html =
                size ? std::string(reinterpret_cast<const char*>(bytes), size) : std::string{};
            self->Next();
          },
          Retain());
    } else if (format == ClipboardReadFormat::Image) {
      gtk_clipboard_request_image(
          board,
          +[](GtkClipboard*, GdkPixbuf* image, gpointer ptr) {
            auto self = Take(ptr);
            if (self->done)
              return;
            gchar* bytes = nullptr;
            gsize size = 0;
            if (!image ||
                !gdk_pixbuf_save_to_buffer(image, &bytes, &size, "png", nullptr, nullptr)) {
              self->Finish(false);
              return;
            }
            self->data.image = Image::FromBase64(
                clipboard_internal::Base64(reinterpret_cast<unsigned char*>(bytes), size));
            g_free(bytes);
            if (!self->data.image) {
              self->Finish(false);
              return;
            }
            self->Next();
          },
          Retain());
    } else {
      gtk_clipboard_request_uris(
          board,
          +[](GtkClipboard*, gchar** uris, gpointer ptr) {
            auto self = Take(ptr);
            if (self->done)
              return;
            if (!uris) {
              self->Finish(false);
              return;
            }
            for (gchar** uri = uris; *uri; ++uri) {
              gchar* host = nullptr;
              gchar* path = g_filename_from_uri(*uri, &host, nullptr);
              if (path && (!host || !*host || g_ascii_strcasecmp(host, "localhost") == 0))
                self->data.file_paths.emplace_back(path);
              g_free(host);
              g_free(path);
            }
            self->Next();
          },
          Retain());
    }
  }
};

struct PublishedData {
  ClipboardData data;
  std::vector<unsigned char> png;
  std::vector<std::string> uris;
  std::vector<std::string> names;
  static void Get(GtkClipboard*, GtkSelectionData* selection, guint info, gpointer ptr) {
    const auto& self = *static_cast<PublishedData*>(ptr);
    if (info == 1)
      gtk_selection_data_set_text(selection, self.data.text->data(),
                                  static_cast<gint>(self.data.text->size()));
    else if (info == 2)
      gtk_selection_data_set(selection, gtk_selection_data_get_target(selection), 8,
                             reinterpret_cast<const guchar*>(self.data.html->data()),
                             static_cast<gint>(self.data.html->size()));
    else if (info == 3)
      gtk_selection_data_set(selection, gtk_selection_data_get_target(selection), 8,
                             self.png.data(), static_cast<gint>(self.png.size()));
    else if (info == 4) {
      std::vector<gchar*> uris;
      for (const auto& uri : self.uris)
        uris.push_back(const_cast<gchar*>(uri.c_str()));
      uris.push_back(nullptr);
      gtk_selection_data_set_uris(selection, uris.data());
    } else if (info == 5) {
      std::string value = "copy";
      for (const auto& uri : self.uris)
        value += "\n" + uri;
      gtk_selection_data_set(selection, gtk_selection_data_get_target(selection), 8,
                             reinterpret_cast<const guchar*>(value.data()),
                             static_cast<gint>(value.size()));
    } else
      gtk_selection_data_set(selection, gtk_selection_data_get_target(selection), 8, nullptr, 0);
  }
};
}  // namespace

struct Clipboard::Impl::Platform {
  GtkClipboard* board = nullptr;
  gulong signal = 0;
};
Clipboard::Impl::Impl(Clipboard* owner) : owner(owner), platform(std::make_unique<Platform>()) {}
Clipboard::Impl::~Impl() {
  Stop();
}
bool Clipboard::IsSupported() {
  return Board() != nullptr;
}
bool Clipboard::IsChangeMonitoringSupported() {
  auto* board = Board();
  return board && gdk_display_supports_selection_notification(gtk_clipboard_get_display(board));
}
void Clipboard::Impl::Read(unsigned mask, std::function<void(bool, ClipboardData)> callback) {
  auto* board = Board();
  if (!board) {
    RunOnMainThread([callback = std::move(callback)] { callback(false, {}); });
    return;
  }
  auto state = std::make_shared<ReadState>(board, mask);
  state->callback = std::move(callback);
  state->Watch();
  RunOnMainThread([state] { state->Start(); });
}
bool Clipboard::Impl::Write(const ClipboardData& data) {
  auto* board = Board();
  if (!board)
    return false;
  auto state = std::make_unique<PublishedData>();
  if ((data.text && data.text->size() > G_MAXINT) || (data.html && data.html->size() > G_MAXINT))
    return false;
  state->data = data;
  std::vector<guint> infos;
  auto add = [&](const char* name, guint info) {
    state->names.emplace_back(name);
    infos.push_back(info);
  };
  if (data.text) {
    add("UTF8_STRING", 1);
    add("text/plain;charset=utf-8", 1);
    add("text/plain", 1);
    add("TEXT", 1);
    add("STRING", 1);
  }
  if (data.html)
    add("text/html", 2);
  if (data.image) {
    state->png = clipboard_internal::DecodeBase64(data.image->ToBase64());
    if (state->png.empty() || state->png.size() > G_MAXINT)
      return false;
    state->data.image.reset();
    add("image/png", 3);
  }
  for (const auto& path : data.file_paths) {
    gchar* uri = g_filename_to_uri(path.c_str(), nullptr, nullptr);
    if (!uri)
      return false;
    state->uris.emplace_back(uri);
    g_free(uri);
  }
  if (!state->uris.empty()) {
    add("text/uri-list", 4);
    add("x-special/gnome-copied-files", 5);
  }
  const bool empty = state->names.empty();
  // First take ownership, even when clearing another application's clipboard.
  if (empty)
    add("application/x-nativeapi-empty", 0);
  std::vector<GtkTargetEntry> targets;
  for (size_t i = 0; i < state->names.size(); ++i)
    targets.push_back({const_cast<gchar*>(state->names[i].c_str()), 0, infos[i]});
  if (!gtk_clipboard_set_with_data(
          board, targets.data(), targets.size(), PublishedData::Get,
          +[](GtkClipboard*, gpointer ptr) { delete static_cast<PublishedData*>(ptr); },
          state.get()))
    return false;
  state.release();
  if (empty)
    gtk_clipboard_clear(board);
  else
    gtk_clipboard_set_can_store(board, nullptr, 0);
  return true;
}
bool Clipboard::Impl::Start() {
  if (!Clipboard::IsChangeMonitoringSupported())
    return false;
  platform->board = Board();
  if (!platform->board)
    return false;
  g_object_ref(platform->board);
  platform->signal =
      g_signal_connect(platform->board, "owner-change",
                       G_CALLBACK(+[](GtkClipboard*, GdkEventOwnerChange*, gpointer ptr) {
                         static_cast<Impl*>(ptr)->Changed();
                       }),
                       this);
  return platform->signal != 0;
}
void Clipboard::Impl::Stop() {
  if (platform->board) {
    if (platform->signal)
      g_signal_handler_disconnect(platform->board, platform->signal);
    g_object_unref(platform->board);
  }
  platform->signal = 0;
  platform->board = nullptr;
  monitoring = false;
}
}  // namespace nativeapi
