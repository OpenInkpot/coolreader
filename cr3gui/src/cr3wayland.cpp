/*
 *	CR3 for OpenInkpot (Wayland)
 * 	Author: Sergiy Kibrik <sakib@meta.ua>, (C) 2016
 *	Based on cr3xcb.c
*/
/* #define NDEBUG */
#include <stdint.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <crengine.h>
#include <crgui.h>
#include <crtrace.h>
#include <crtest.h>
#include <signal.h>

#include <linux/input.h>

#include "cr3wayland.h"

#include <locale.h>
#include <libintl.h>
#include <cri18n.h>

//FIXME: must be defined by build system
#define SYS_DATA_DIR	"/usr/share/cr3"
#define EXT_DATA_DIR	"/media/sd/crengine"
#define CACHE_DIR	"/.cache/cr3"

static CRWLWindowManager *sigwm = NULL;

static void sighandler(int signal)
{
	CRLog::debug("received signal %s", strsignal(signal));
	if (sigwm)
		sigwm->disconnect();
}

CRWLBuffer::~CRWLBuffer()
{
	lUInt8 *data = GetScanLine(0);
	int size = GetRowSize() * GetHeight();
	if (-1 == munmap(static_cast<void*>(data), size))
		CRLog::error("%s: munmap() failed: %m", __func__);
	CRLog::trace("%s: cleaned-up resources", __func__);
}

CRWLBuffer * CRWLScreen::createBuffer(struct wl_shm *shm, int dx, int dy,
				const lString8 &name)
{
	CRLog::trace("CRWLScreen: %s(%d,%d)", __func__, dx, dy);
	lString8 shm_name("/cr3-wl_shm-");
	shm_name.append(name);

	int fd = shm_open(shm_name.c_str(), O_RDWR|O_CREAT|O_EXCL,
			  S_IRUSR|S_IWUSR);
	if (fd < 0) {
		CRLog::error("%s:failed to create shared mem object: %m");
		return NULL;
	}

	/*FIXME: correct color depth*/
	int stride = dx * 2;
	int size = stride * dy;
	if (ftruncate(fd, size) < 0) {
		close(fd);
		shm_unlink(shm_name.c_str());
		CRLog::error("%s: failed to create shared mem: %m\n");
		return NULL;
	}
	void *data = mmap(NULL, size, PROT_READ | PROT_WRITE,
			  MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		CRLog::error("%s: failed to map mem: %m\n");
		close(fd);
		shm_unlink(shm_name.c_str());
		return NULL;
	}

	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
	/*FIXME: correct format */
	struct wl_buffer *buffer =
		wl_shm_pool_create_buffer(pool, 0, dx, dy, stride,
				  WL_SHM_FORMAT_RGB565);

	/*FIXME: correct color depth*/
	CRWLBuffer *buf = new CRWLBuffer(dx, dy, 16, static_cast<lUInt8*>(data),
					 buffer, shm_name);
	buf->Clear(0xFFFFFF);

	wl_shm_pool_destroy(pool);
	close(fd);
	shm_unlink(shm_name.c_str());

	return buf;
}

CRWLScreen::CRWLScreen(int width, int height,
	   struct wl_compositor *compositor,
	   struct wl_shell *shell,
	   struct wl_shm *shm,
	   CRWLWindowManager &wm)
	: CRGUIScreenBase(0, 0, false),
	  _surface(NULL), _shell_surface(NULL), _callback(NULL), _wm(wm)
{
	CRLog::trace("CRWLScreen(%d,%d)", width, height);

	static const struct wl_surface_listener surfaceListener = {
		&CRWLScreen::cbHandleEnter,
		&CRWLScreen::cbHandleLeave
	};

	static const struct wl_shell_surface_listener shellSurfaceListener = {
		&CRWLScreen::cbHandlePing,
		&CRWLScreen::cbHandleConfigure,
		&CRWLScreen::cbHandlePopupDone,
	};
	_width = width;
	_height = height;
	_surface = wl_compositor_create_surface(compositor);
	wl_surface_add_listener(_surface, &surfaceListener, this);

	_shell_surface = wl_shell_get_shell_surface(shell, _surface);
	wl_shell_surface_add_listener(_shell_surface,
				      &shellSurfaceListener, this);
	wl_shell_surface_set_toplevel(_shell_surface);

	_front = NULL;
	_canvas = createBuffer(shm, width, height, lString8("canvas"));
	if (!_canvas) {
		CRLog::error("%s: failed to create canvas");
		/*TODO: exception? */
	}
}

CRWLScreen::~CRWLScreen()
{
	CRLog::trace("~CRWLScreen()");
}

void CRWLScreen::handleFrame(struct wl_callback *callback, uint32_t time)
{
	if (callback)
		wl_callback_destroy(callback);
	CRLog::trace("%s: frame done", __func__);
	_wm.postEvent(new CRGUIUpdateEvent(false));
}

/*TODO: implement this correctly */
bool CRWLScreen::setSize(int dx, int dy)
{
	CRLog::error("CRWLScreen::setSize(%d,%d) not implemented\n", dx, dy);
	return false;
}

void CRWLScreen::update(const lvRect &a_rc, bool full)
{
	CRLog::debug("CRWLScreen: update([%d,%d,%d,%d],%d)",
		a_rc.left, a_rc.top, a_rc.right, a_rc.bottom, full);

	if (!_wm.is_connected())
		return;

	/*FIXME: hack around LVRef */
	CRWLBuffer *front = static_cast<CRWLBuffer*>(_canvas.get());

	wl_surface_attach(_surface, front->_buffer, 0, 0);
	if (full)
		wl_surface_damage(_surface, 0, 0,
			  _width, _height);
	else
		wl_surface_damage(_surface, a_rc.left, a_rc.top,
			  a_rc.width(), a_rc.height());

	static const struct wl_callback_listener frameListener = {
		&CRWLScreen::cbHandleFrame,
	};

	_callback = wl_surface_frame(_surface);
	wl_callback_add_listener(_callback, &frameListener, this);
	wl_surface_commit(_surface);
}

void CRWLScreen::handleEnter(struct wl_surface *surface, struct wl_output *output)
{
	CRLog::trace("%s: surface %p enered output %p",
		     __func__, surface, output);
}

void CRWLScreen::handleLeave(struct wl_surface *surface, struct wl_output *output)
{
	CRLog::trace("%s: surface %p left output %p",
		     __func__, surface, output);
}

void CRWLScreen::handlePing(struct wl_shell_surface *shell_surface,
			    uint32_t serial)
{
	wl_shell_surface_pong(shell_surface, serial);
}

void CRWLScreen::handleConfigure(wl_shell_surface *shell_surface,
				 uint32_t edges, int32_t width, int32_t height)
{
	CRLog::trace("%s: edges=%u, size=%dx%d", __func__,
			edges, width, height);
}

void CRWLScreen::handlePopupDone(wl_shell_surface *shell_surface)
{
	CRLog::trace("%s", __func__);
}

CRWLWindowManager::CRWLWindowManager()
	: CRGUIWindowManager(NULL),
	  _display(NULL), _registry(NULL), _compositor(NULL),
	  _shell(NULL), _seat(NULL), _shm(NULL), _output(NULL), _input(NULL),
	  _width(320), _height(480)
{
	_display = wl_display_connect(NULL);
	if (!_display) {
		CRLog::error("%s: can't connect display", __func__);
		/*TODO: throw exception instead */
		return;
	}
	static const struct wl_registry_listener registryListener = {
		&CRWLWindowManager::cbHandleRegistry,
		&CRWLWindowManager::cbHandleRegistryRemove,
	};

	_registry = wl_display_get_registry(_display);
	wl_registry_add_listener(_registry, &registryListener, this);
	wl_display_roundtrip(_display);

	/* second roundtrip for shm to process format events */
	wl_display_roundtrip(_display);
	assert(_shm != NULL);
	assert(_output != NULL);
	_screen = new CRWLScreen(_width, _height, _compositor,
				 _shell, _shm, *this);
}

CRWLWindowManager::~CRWLWindowManager()
{
	delete _screen;
	disconnect();
	CRLog::trace("%s: cleaned-up resources", __func__);
}

bool CRWLWindowManager::onKeyPressed(int key, int flags)
{
	CRLog::trace("CRWLWindowManager::onKeyPressed(%#x,%#x)",
		key, flags);
	return CRGUIWindowManager::onKeyPressed(key, flags);
}

void CRWLWindowManager::forwardSystemEvents(bool waitForEvent)
{
	CRLog::trace("CRWLWindowManager::forwardSystemEvents(%d)",
		waitForEvent);

	if (_stopFlag)
		return;
	if (waitForEvent)
		wl_display_dispatch(_display);
	else
		wl_display_dispatch_pending(_display);
}

void CRWLWindowManager::disconnect()
{
	_stopFlag = true;
	if (!_display)
		return;
	delete _input;
	_input = NULL;
	wl_display_disconnect(_display);
	_display = NULL;
	CRLog::trace("%s: done", __func__);
}

void CRWLWindowManager::handleRegistry(wl_registry *registry, uint32_t id,
		    const lString8 &interface, uint32_t version)
{
	static const struct wl_output_listener outputListener = {
		&CRWLWindowManager::cbHandleGeometry,
		&CRWLWindowManager::cbHandleMode,
	};

	static const struct wl_seat_listener seatListener = {
		&CRWLWindowManager::cbHandleInCaps,
		&CRWLWindowManager::cbHandleInName,
	};

	CRLog::debug("%s: register %s", __func__, interface.c_str());
	if (!interface.compare("wl_compositor")) {
		void *ptr = wl_registry_bind(registry, id,
			&wl_compositor_interface, 1);
		_compositor = static_cast<struct wl_compositor*>(ptr);
	} else if (!interface.compare("wl_shell")) {
		void *ptr = wl_registry_bind(registry, id,
			&wl_shell_interface, 1);
		_shell = static_cast<struct wl_shell*>(ptr);
	} else if (!interface.compare("wl_seat")) {
		void *ptr = wl_registry_bind(registry, id,
			&wl_seat_interface, 1);
		_seat = static_cast<struct wl_seat*>(ptr);
		wl_seat_add_listener(_seat, &seatListener, this);
	} else if (!interface.compare("wl_shm")) {
		void *ptr = wl_registry_bind(registry, id,
			&wl_shm_interface, 1);
		_shm = static_cast<struct wl_shm*>(ptr);
	} else if (!interface.compare("wl_output")) {
		void *ptr = wl_registry_bind(registry, id,
			&wl_output_interface, 1);
		_output = static_cast<struct wl_output*>(ptr);
		wl_output_add_listener(_output, &outputListener, this);
	}
}

void CRWLWindowManager::handleRegistryRemove(wl_registry *, uint32_t id)
{
	CRLog::debug("%s: un-register %u", __func__, id);
}
void CRWLWindowManager::handleGeometry(struct wl_output *output,
			int32_t x, int32_t y,
			int32_t physical_width, int32_t physical_height,
			int32_t subpixel, const char *make, const char *model,
			int32_t output_transform)
{
	assert(output == _output);
	/*TODO: make good use of these */
	CRLog::debug("%s: x=%d, y=%d, physical_width=%d, physical_height=%d",
		     __func__, x, y, physical_width, physical_height);
	CRLog::debug("%s: subpixel=%#x, make=%s, model=%s, transform=%#x",
		     __func__, subpixel, make, model, output_transform);
}

void CRWLWindowManager::handleMode(struct wl_output *output, uint32_t flags,
			int32_t width, int32_t height, int32_t refresh)
{
	assert(output == _output);
	if (flags & (WL_OUTPUT_MODE_PREFERRED|WL_OUTPUT_MODE_CURRENT)) {
		_width = width;
		_height = height;
	}

	CRLog::debug("%s: flags=%#x, width=%d, height=%d, refresh=%d",
		     __func__, flags, width, height, refresh);
}

void CRWLWindowManager::handleInputCaps(struct wl_seat *seat,
					enum wl_seat_capability caps)
{
	CRLog::debug("%s: %x (%p)", __func__, caps, seat);
	_input = new CRWLInput(*this, seat, caps);

}

void CRWLWindowManager::handleInputName(struct wl_seat *seat, const char *name)
{
	CRLog::debug("%s: %s (%p)", __func__, name, seat);
}

CRWLInput::CRWLInput(CRWLWindowManager &wm, struct wl_seat *seat,
		     enum wl_seat_capability caps)
		: _wm(wm), _keyboard(NULL)
{
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !_keyboard) {
		static const struct wl_keyboard_listener keyboardListener = {
			&CRWLInput::cbHandleKeymap,
			&CRWLInput::cbHandleEnter,
			&CRWLInput::cbHandleLeave,
			&CRWLInput::cbHandleKey,
			&CRWLInput::cbHandleMod,
			//&CRWLInput::cbHandleRepeatInfo,
		};
		_keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(_keyboard, &keyboardListener, this);
	}
}

CRWLInput::~CRWLInput()
{
}

void CRWLInput::handleKeymap(struct wl_keyboard *keyboard, uint32_t format,
			     int fd, uint32_t size)
{}

void CRWLInput::handleEnter(struct wl_keyboard *keyboard, uint32_t serial,
			 struct wl_surface *surface, struct wl_array *keys)
{}

void CRWLInput::handleLeave(struct wl_keyboard *keyboard, uint32_t serial,
			    struct wl_surface *surface)
{
}

void CRWLInput::handleKey(struct wl_keyboard *keyboard,
			  uint32_t serial, uint32_t time, uint32_t key,
			  uint32_t state_w)
{
	CRLog::trace("%s: %u %u", __func__, serial, key);
	// 'key' is evdev keycode, see linux/input.h
	_wm.postEvent(new CRGUIKeyDownEvent(key, state_w));
}

void CRWLInput::handleMod(struct wl_keyboard *keyboard,
                          uint32_t serial, uint32_t mods_depressed,
                          uint32_t mods_latched, uint32_t mods_locked,
                          uint32_t group)
{}

class WLDocViewWin : public V3DocViewWin
{
public:
	virtual void OnLoadFileEnd()
	{
		CRLog::trace("WLDocViewWin::OnLoadFileEnd()");
		_wm->update(true);
		V3DocViewWin::OnLoadFileEnd();
	}

	WLDocViewWin(CRGUIWindowManager *wm)
	: V3DocViewWin(wm, NULL)
	{
		CRLog::trace("WLDocViewWin(%p)", (void*)wm);
	}
};

int main(int argc, char *argv[])
{
	CRLog::setStdoutLogger();
	CRLog::setLogLevel(CRLog::LL_TRACE);
	/*TODO: cfg file*/
	/*InitCREngineLog("/home/user/.crengine/crlog.ini");*/

	lString16Collection fontDirs;
	if (!InitCREngine(argv[0], fontDirs)) {
		fprintf(stderr, "cannot init CREngine - exiting\n");
		return 2;
	}

	if (argc < 2) {
		fprintf(stderr, "usage: cr3 <filename_to_open>\n");
		return 3;
	}

	if (!strcmp(argv[1], "unittest")) {
		runCRUnitTests();
		return 0;
	}

	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGTERM);
	sigaddset(&set, SIGINT);
	sigprocmask(SIG_BLOCK, &set, NULL);
	CRWLWindowManager *winman = sigwm = new CRWLWindowManager;
	struct sigaction act;
	act.sa_handler = sighandler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGINT, &act, NULL);
	sigprocmask(SIG_UNBLOCK, &set, NULL);

	if (!winman->is_connected()) {
		CRLog::error("no display connection");
		ShutdownCREngine();
		return ENOENT;
	}

	const char *keymap_locations[] = {
		EXT_DATA_DIR "/keymaps",
		SYS_DATA_DIR "/keymaps",
		NULL,
	};
	loadKeymaps(*winman, keymap_locations);

	const char *data_dirs[] = {
		EXT_DATA_DIR,
		SYS_DATA_DIR,
		NULL
	};

	for (const char **dir = data_dirs; *dir; dir++) {
		lString16 skin = lString16(*dir) + L"/skins/default";
		if (winman->loadSkin(lString16(skin)))
			break;
	}

	const lChar16 * imgname = L"cr3_logo_screen.png";
	LVImageSourceRef img = winman->getSkin()->getImage(imgname);
	if (!img.isNull()) {
		winman->getScreen()->getCanvas()->Draw(img, 0, 0,
			winman->getScreen()->getWidth(),
			winman->getScreen()->getHeight(), false);
	}

	ldomDocCache::init(lString16(getenv("HOME")) + CACHE_DIR,
			   0x100000 * 32 ); /* 32Mb */
	V3DocViewWin *main_win = new WLDocViewWin(winman);
	main_win->getDocView()->setBackgroundColor(0xFFFFFF);
	main_win->getDocView()->setTextColor(0x000000);
	main_win->getDocView()->setFontSize( 20 );
	/*FIXME: install & use css */
	/*main_win->loadCSS(lString16( L"/usr/share/cr3/fb2.css"));*/

	for (const char **dir = data_dirs; *dir; dir++) {
		lString16 ini = lString16(*dir) + L"/cr3.ini";
		if (main_win->loadSettings(ini))
			break;
	}

	winman->activateWindow(main_win);
	if (main_win->loadDocument(LocalToUnicode(lString8(argv[1]))))
		winman->runEventLoop();
	else
		CRLog::error("can't open %s\n", argv[1]);

	sigwm = NULL;
	delete winman;

	ShutdownCREngine();
	return 0;
}
