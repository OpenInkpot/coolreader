#ifndef CR3WAYLAND_INCLUDED
#define CR3WAYLAND_INCLUDED

#include <wayland-client.h>

#include "cr3main.h"
#include "mainwnd.h"

class CRWLInput;

class CRWLBuffer : public LVColorDrawBuf
{
public:
	struct wl_buffer *_buffer;
	lString8 _name;

	CRWLBuffer(int dx, int dy, int bpp, lUInt8 *auxdata,
		   struct wl_buffer *buffer, lString8 name)
	: LVColorDrawBuf(dx, dy, auxdata, bpp), _buffer(buffer), _name(name)
	{}
	virtual ~CRWLBuffer();
};

class CRWLWindowManager : public CRGUIWindowManager
{
public:
	CRWLWindowManager();
	virtual ~CRWLWindowManager();
	virtual bool onKeyPressed(int key, int flags = 0);
	virtual void forwardSystemEvents(bool waitForEvent);
	void disconnect();
	bool is_connected() { return _display != NULL; }

protected:
	/*TODO:*/
private:
	void handleRegistry(wl_registry *registry, uint32_t id,
			    const lString8 &interface, uint32_t version);
	void handleRegistryRemove(wl_registry *, uint32_t id);
	void handleGeometry(struct wl_output *output, int32_t x, int32_t y,
			int32_t physical_width, int32_t physical_height,
			int32_t subpixel, const char *make, const char *model,
			int32_t output_transform);
	void handleMode(struct wl_output *output,
			uint32_t flags, int32_t width, int32_t height,
			int32_t refresh);
	void handleInputCaps(struct wl_seat *seat, enum wl_seat_capability caps);
	void handleInputName(struct wl_seat *seat, const char *name);

	static void cbHandleRegistry(void *data, wl_registry *registry,
				uint32_t id, const char *interface,
				uint32_t version)
	{
		static_cast<CRWLWindowManager*>(data)->handleRegistry(registry,
					id, lString8(interface), version);
	}

	static void cbHandleRegistryRemove(void *data, wl_registry *registry,
						uint32_t name)
	{
		static_cast<CRWLWindowManager*>(data)->handleRegistryRemove(registry, name);
	}

	static void cbHandleGeometry(void *data, struct wl_output *output,
			int32_t x, int32_t y,
			int32_t physical_width, int32_t physical_height,
			int32_t subpixel,
			const char *make, const char *model,
			int32_t output_transform)
	{
		static_cast<CRWLWindowManager*>(data)->handleGeometry(output,
			x, y, physical_width, physical_height, subpixel,
			make, model, output_transform);
	}

	static void cbHandleMode(void *data, struct wl_output *output,
			uint32_t flags, int32_t width, int32_t height,
			int32_t refresh)
	{
		static_cast<CRWLWindowManager*>(data)->handleMode(output,
			flags, width, height, refresh);
	}

	static void cbHandleInCaps(void *data, struct wl_seat *seat,
				   uint32_t caps)
	{
		enum wl_seat_capability c = static_cast<wl_seat_capability>(caps);
		static_cast<CRWLWindowManager*>(data)->handleInputCaps(seat, c);
	}

	static void cbHandleInName(void *data, struct wl_seat *seat,
				   const char *name)
	{
		static_cast<CRWLWindowManager*>(data)->handleInputName(seat, name);
	};

	struct wl_display *_display;
	struct wl_registry *_registry;
	struct wl_compositor *_compositor;
	struct wl_shell *_shell;
	struct wl_seat *_seat;
	struct wl_shm *_shm;
	struct wl_output *_output;
	CRWLInput *_input;
	uint32_t _width;
	uint32_t _height;
};

class CRWLInput {
public:
	CRWLInput(CRWLWindowManager&, struct wl_seat*, enum wl_seat_capability);
	~CRWLInput();
private:
	void handleKeymap(struct wl_keyboard*, uint32_t, int, uint32_t);
	void handleEnter(struct wl_keyboard *keyboard, uint32_t serial,
			 struct wl_surface *surface, struct wl_array *keys);
	void handleLeave(struct wl_keyboard*, uint32_t, struct wl_surface*);
	void handleKey(struct wl_keyboard*, uint32_t, uint32_t,
		       uint32_t, uint32_t);
	void handleMod(struct wl_keyboard*, uint32_t, uint32_t, uint32_t,
		       uint32_t, uint32_t);

	static void cbHandleKeymap(void *data, struct wl_keyboard *keyboard,
				   uint32_t format, int fd, uint32_t size)
	{
		static_cast<CRWLInput*>(data)->handleKeymap(keyboard, format,
							    fd, size);
	}

	static void cbHandleEnter(void *data, struct wl_keyboard *keyboard,
				  uint32_t serial, struct wl_surface *surface,
				  struct wl_array *keys)
	{
		static_cast<CRWLInput*>(data)->handleEnter(keyboard, serial,
							   surface, keys);
	}

	static void cbHandleLeave(void *data, struct wl_keyboard *keyboard,
				  uint32_t serial, struct wl_surface *surface)
	{
		static_cast<CRWLInput*>(data)->handleLeave(keyboard, serial,
							   surface);
	}

	static void cbHandleKey(void *data, struct wl_keyboard *keyboard,
				uint32_t serial, uint32_t time, uint32_t key,
				uint32_t state_w)
	{
		static_cast<CRWLInput*>(data)->handleKey(keyboard, serial,
						time, key, state_w);
	}

	static void cbHandleMod(void *data, struct wl_keyboard *keyboard,
                          uint32_t serial, uint32_t mods_depressed,
                          uint32_t mods_latched, uint32_t mods_locked,
                          uint32_t group)
	{
		static_cast<CRWLInput*>(data)->handleMod(keyboard, serial,
						mods_depressed, mods_latched,
						mods_locked, group);
	}

//	static void cbHandleRepeatInfo(void *data, struct wl_keyboard *keyboard,
//				       int32_t rate, int32_t delay)
//	{
//	}

	CRWLInput(CRWLInput&);
	CRWLWindowManager &_wm;
	struct wl_keyboard *_keyboard;
};

class CRWLScreen : public CRGUIScreenBase
{
public:
	CRWLScreen(int width, int height,
		   struct wl_compositor *compositor,
		   struct wl_shell *shell,
		   struct wl_shm *shm,
		   CRWLWindowManager& wm);

	virtual ~CRWLScreen();

protected:
	virtual bool setSize(int dx, int dy);
	virtual void update(const lvRect &a_rc, bool full);
private:
	CRWLBuffer *createBuffer(struct wl_shm*, int, int, const lString8&);
	void handleFrame(struct wl_callback *callback, uint32_t time);
	void handleEnter(struct wl_surface *surface, struct wl_output *output);
	void handleLeave(struct wl_surface *surface, struct wl_output *output);
	void handlePing(struct wl_shell_surface *shell_surface, uint32_t serial);
	void handleConfigure(wl_shell_surface *shell_surface, uint32_t edges,
			     int32_t width, int32_t height);
	void handlePopupDone(wl_shell_surface *shell_surface);
	static void cbHandleFrame(void *data, struct wl_callback *callback,
				  uint32_t time)
	{
		static_cast<CRWLScreen*>(data)->handleFrame(callback, time);
	}
	static void cbHandleEnter(void *data, struct wl_surface *surface,
				  struct wl_output *output)
	{
		static_cast<CRWLScreen*>(data)->handleEnter(surface, output);
	}
	static void cbHandleLeave(void *data, struct wl_surface *surface,
				  struct wl_output *output)
	{
		static_cast<CRWLScreen*>(data)->handleLeave(surface, output);
	}
	static void cbHandlePing(void *data, wl_shell_surface *shell_surface,
				 uint32_t serial)
	{
		static_cast<CRWLScreen*>(data)->handlePing(shell_surface, serial);
	}
	static void cbHandleConfigure(void *data, wl_shell_surface *shell_surface,
				      uint32_t edges, int32_t width, int32_t height)
	{
		static_cast<CRWLScreen*>(data)->handleConfigure(shell_surface,
							edges, width, height);
	}
	static void cbHandlePopupDone(void *data, wl_shell_surface *shell_surface)
	{
		static_cast<CRWLScreen*>(data)->handlePopupDone(shell_surface);
	}

	struct wl_surface *_surface;
	struct wl_shell_surface *_shell_surface;
	struct wl_callback *_callback;
	CRWLWindowManager& _wm;
};

#endif // CR3WAYLAND_INCLUDED
