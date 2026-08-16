use std::{
    num::NonZeroU32,
    sync::{
        Arc,
        atomic::{AtomicBool, Ordering},
    },
    time::{Duration, Instant},
};

use anyhow::{Context, Result, anyhow};
use signal_hook::{
    consts::{SIGHUP, SIGINT, SIGQUIT, SIGTERM},
    flag,
};
use smithay_client_toolkit::{
    compositor::{CompositorHandler, CompositorState, FrameCallbackData},
    delegate_registry,
    output::{OutputHandler, OutputState},
    reexports::{calloop::EventLoop, calloop_wayland_source::WaylandSource},
    registry::{ProvidesRegistryState, RegistryState},
    registry_handlers,
    seat::{
        Capability, SeatHandler, SeatState,
        keyboard::{KeyEvent, KeyboardHandler, Keysym, Modifiers, RawModifiers},
        pointer::{PointerEvent, PointerEventKind, PointerHandler},
    },
    shell::{
        WaylandSurface,
        wlr_layer::{
            Anchor, KeyboardInteractivity, Layer, LayerShell, LayerShellHandler, LayerSurface,
            LayerSurfaceConfigure,
        },
    },
    shm::{
        Shm, ShmHandler,
        slot::{Buffer, SlotPool},
    },
};
use wayland_client::{
    Connection, QueueHandle,
    globals::registry_queue_init,
    protocol::{wl_keyboard, wl_output, wl_pointer, wl_seat, wl_shm, wl_surface},
};

use crate::{
    engine::Animation,
    options::Options,
    raster::{Damage, Rasterizer},
    vt::Cell,
};

const PRESENT_RATE: u32 = 30;
const NAMESPACE: &str = "org.omarchy.screensaver";

#[derive(Clone, Copy)]
enum KeyboardTransition {
    FocusLeft,
    KeyPressed,
}

impl KeyboardTransition {
    const fn dismisses(self) -> bool {
        matches!(self, Self::KeyPressed)
    }
}

struct OutputSurface {
    output: wl_output::WlOutput,
    layer: LayerSurface,
    width: i32,
    height: i32,
    scale: i32,
    configured: bool,
    dirty: bool,
    frame_ready: bool,
    buffer: Option<Buffer>,
    buffer_size: (i32, i32),
    rendered_cells: Vec<Cell>,
    rendered_alpha: u8,
    rendered_grid: (i32, i32),
}

impl OutputSurface {
    fn matches(&self, surface: &wl_surface::WlSurface) -> bool {
        self.layer.wl_surface() == surface
    }
}

struct App {
    registry_state: RegistryState,
    seat_state: SeatState,
    output_state: OutputState,
    compositor: CompositorState,
    layer_shell: LayerShell,
    shm: Shm,
    pool: SlotPool,
    surfaces: Vec<OutputSurface>,
    keyboard: Option<wl_keyboard::WlKeyboard>,
    pointer: Option<wl_pointer::WlPointer>,
    last_pointer: Option<(f64, f64)>,
    rasterizer: Rasterizer,
    animation: Option<Animation>,
    input: String,
    options: Options,
    started: Instant,
    dismissing: Option<(Instant, f32)>,
    next_engine_tick: Instant,
    next_present: Instant,
    last_frame_hash: u64,
    last_alpha: u8,
    terminated: Arc<AtomicBool>,
    exit: bool,
    failure: Option<anyhow::Error>,
}

pub fn run(input: String, options: Options) -> Result<()> {
    let connection = Connection::connect_to_env().context("connecting to Wayland")?;
    let (globals, event_queue) =
        registry_queue_init(&connection).context("reading Wayland globals")?;
    let qh = event_queue.handle();
    let mut event_loop: EventLoop<App> = EventLoop::try_new().context("creating event loop")?;
    WaylandSource::new(connection, event_queue)
        .insert(event_loop.handle())
        .context("registering Wayland event source")?;

    let compositor = CompositorState::bind(&globals, &qh).context("wl_compositor unavailable")?;
    let layer_shell = LayerShell::bind(&globals, &qh).context("wlr-layer-shell unavailable")?;
    let shm = Shm::bind(&globals, &qh).context("wl_shm unavailable")?;
    let pool = SlotPool::new(4, &shm).context("creating shared-memory pool")?;
    let terminated = Arc::new(AtomicBool::new(false));
    for signal in [SIGINT, SIGTERM, SIGHUP, SIGQUIT] {
        flag::register(signal, Arc::clone(&terminated)).context("installing signal handler")?;
    }
    let now = Instant::now();
    let mut app = App {
        registry_state: RegistryState::new(&globals),
        seat_state: SeatState::new(&globals, &qh),
        output_state: OutputState::new(&globals, &qh),
        compositor,
        layer_shell,
        shm,
        pool,
        surfaces: Vec::new(),
        keyboard: None,
        pointer: None,
        last_pointer: None,
        rasterizer: Rasterizer::load()?,
        animation: None,
        input,
        options,
        started: now,
        dismissing: None,
        next_engine_tick: now,
        next_present: now,
        last_frame_hash: 0,
        last_alpha: 0,
        terminated,
        exit: false,
        failure: None,
    };

    while !app.exit {
        let now = Instant::now();
        let timeout = app
            .next_engine_tick
            .saturating_duration_since(now)
            .min(Duration::from_millis(8));
        event_loop
            .dispatch(timeout, &mut app)
            .context("dispatching Wayland events")?;
        app.tick(&qh);
        if let Some(error) = app.failure.take() {
            return Err(error);
        }
    }
    Ok(())
}

impl App {
    fn add_output(&mut self, output: wl_output::WlOutput, qh: &QueueHandle<Self>) {
        if self.surfaces.iter().any(|surface| surface.output == output) {
            return;
        }
        let wl_surface = self.compositor.create_surface(qh);
        let layer = self.layer_shell.create_layer_surface(
            qh,
            wl_surface,
            Layer::Overlay,
            Some(NAMESPACE),
            Some(&output),
        );
        layer.set_anchor(Anchor::TOP | Anchor::BOTTOM | Anchor::LEFT | Anchor::RIGHT);
        layer.set_size(0, 0);
        layer.set_exclusive_zone(-1);
        layer.set_keyboard_interactivity(KeyboardInteractivity::Exclusive);
        layer.commit();
        self.surfaces.push(OutputSurface {
            output,
            layer,
            width: 1,
            height: 1,
            scale: 1,
            configured: false,
            dirty: true,
            frame_ready: true,
            buffer: None,
            buffer_size: (0, 0),
            rendered_cells: Vec::new(),
            rendered_alpha: 0,
            rendered_grid: (0, 0),
        });
    }

    fn keyboard_transition(&mut self, transition: KeyboardTransition) {
        if transition.dismisses() {
            self.request_dismiss();
        }
    }

    fn request_dismiss(&mut self) {
        if self.dismissing.is_some() {
            return;
        }
        let now = Instant::now();
        self.dismissing = Some((now, self.opacity(now)));
        self.next_present = now;
        for surface in &mut self.surfaces {
            surface.dirty = true;
        }
    }

    fn opacity(&self, now: Instant) -> f32 {
        if let Some((at, from)) = self.dismissing {
            let elapsed = now.saturating_duration_since(at).as_millis() as f32;
            return from * (1.0 - elapsed / self.options.fade_out_ms as f32).clamp(0.0, 1.0);
        }
        let elapsed = now.saturating_duration_since(self.started).as_millis() as f32;
        (elapsed / self.options.fade_ms as f32).clamp(0.0, 1.0)
    }

    fn tick(&mut self, qh: &QueueHandle<Self>) {
        let now = Instant::now();
        if self.terminated.load(Ordering::Relaxed) {
            self.request_dismiss();
        }
        if self.dismissing.is_some() && self.opacity(now) <= 0.0 {
            self.exit = true;
            return;
        }
        if let Err(error) = self.ensure_animation() {
            self.failure = Some(error);
            self.exit = true;
            return;
        }
        let frame_duration = Duration::from_secs_f64(1.0 / self.options.frame_rate.max(1) as f64);
        let mut advanced = false;
        let mut catchup = 0;
        while now >= self.next_engine_tick && catchup < 8 {
            if let Some(animation) = &mut self.animation {
                if let Err(error) = animation.advance() {
                    self.failure = Some(error);
                    self.exit = true;
                    return;
                }
                advanced = true;
            }
            self.next_engine_tick += frame_duration;
            catchup += 1;
        }
        if catchup == 8 && now >= self.next_engine_tick {
            self.next_engine_tick = now + frame_duration;
        }
        let alpha = (self.opacity(now) * 255.0).round() as u8;
        let frame_hash = self
            .animation
            .as_ref()
            .map_or(0, |animation| hash_cells(animation.cells()));
        if (advanced && frame_hash != self.last_frame_hash) || alpha != self.last_alpha {
            self.last_frame_hash = frame_hash;
            self.last_alpha = alpha;
            for surface in &mut self.surfaces {
                surface.dirty = true;
            }
        }
        self.present_dirty(qh, false);
    }

    fn ensure_animation(&mut self) -> Result<()> {
        let Some((cols, rows)) = self
            .surfaces
            .iter()
            .filter(|surface| surface.configured)
            .map(|surface| {
                let (width, height) = (
                    surface.width * surface.scale,
                    surface.height * surface.scale,
                );
                self.rasterizer.grid_size(width, height, surface.scale)
            })
            .max_by_key(|(cols, rows)| i64::from(*cols) * i64::from(*rows))
        else {
            return Ok(());
        };
        let cols = if self.options.cols > 0 {
            self.options.cols
        } else {
            cols
        };
        let rows = if self.options.rows > 0 {
            self.options.rows
        } else {
            rows
        };
        match &mut self.animation {
            Some(animation) => animation.resize(cols, rows)?,
            None => {
                self.animation = Some(Animation::new(
                    self.input.clone(),
                    self.options.clone(),
                    cols,
                    rows,
                )?);
            }
        }
        Ok(())
    }

    fn present_dirty(&mut self, qh: &QueueHandle<Self>, force: bool) {
        let now = Instant::now();
        if !force && now < self.next_present {
            return;
        }
        let Some(animation) = &self.animation else {
            return;
        };
        let opacity = self.opacity(now);
        let (cells, cols, rows) = (animation.cells(), animation.cols(), animation.rows());
        let mut committed = false;
        for surface in &mut self.surfaces {
            if !surface.configured || !surface.dirty || !surface.frame_ready {
                continue;
            }
            match present_surface(
                &mut self.pool,
                &mut self.rasterizer,
                surface,
                qh,
                cells,
                cols,
                rows,
                opacity,
            ) {
                Ok(did_commit) => committed |= did_commit,
                Err(error) => {
                    self.failure = Some(error);
                    self.exit = true;
                    return;
                }
            }
        }
        if committed {
            self.next_present = now + Duration::from_secs_f64(1.0 / PRESENT_RATE as f64);
        }
    }
}

#[allow(clippy::too_many_arguments)]
fn present_surface(
    pool: &mut SlotPool,
    rasterizer: &mut Rasterizer,
    surface: &mut OutputSurface,
    qh: &QueueHandle<App>,
    cells: &[Cell],
    cols: i32,
    rows: i32,
    opacity: f32,
) -> Result<bool> {
    let width = surface.width * surface.scale;
    let height = surface.height * surface.scale;
    let stride = width * 4;
    if surface.buffer_size != (width, height) {
        surface.buffer = None;
        surface.buffer_size = (width, height);
        surface.rendered_cells.clear();
    }

    let damage = if surface.buffer.is_none() {
        let (buffer, canvas) = pool
            .create_buffer(width, height, stride, wl_shm::Format::Argb8888)
            .context("creating layer buffer")?;
        let damage = raster_surface(
            rasterizer, surface, canvas, width, height, cells, cols, rows, opacity,
        )?;
        buffer
            .attach_to(surface.layer.wl_surface())
            .context("attaching layer buffer")?;
        surface.buffer = Some(buffer);
        damage
    } else {
        let Some(canvas) = pool.canvas(surface.buffer.as_ref().expect("buffer exists")) else {
            return Ok(false);
        };
        let damage = raster_surface(
            rasterizer, surface, canvas, width, height, cells, cols, rows, opacity,
        )?;
        let Some(damage) = damage else {
            surface.dirty = false;
            return Ok(false);
        };
        surface
            .buffer
            .as_ref()
            .expect("buffer exists")
            .attach_to(surface.layer.wl_surface())
            .context("attaching layer buffer")?;
        Some(damage)
    };
    let Some(damage) = damage else {
        surface.dirty = false;
        return Ok(false);
    };
    surface
        .layer
        .wl_surface()
        .damage_buffer(damage.x, damage.y, damage.width, damage.height);
    surface
        .layer
        .wl_surface()
        .frame(qh, FrameCallbackData(surface.layer.wl_surface().clone()));
    surface.layer.commit();
    surface.dirty = false;
    surface.frame_ready = false;
    Ok(true)
}

#[allow(clippy::too_many_arguments)]
fn raster_surface(
    rasterizer: &mut Rasterizer,
    surface: &mut OutputSurface,
    pixels: &mut [u8],
    width: i32,
    height: i32,
    cells: &[Cell],
    cols: i32,
    rows: i32,
    opacity: f32,
) -> Result<Option<Damage>> {
    let alpha = (opacity.clamp(0.0, 1.0) * 255.0).round() as u8;
    let can_update_incrementally = alpha == 255
        && surface.rendered_alpha == 255
        && surface.rendered_grid == (cols, rows)
        && surface.rendered_cells.len() == cells.len();
    let damage = if can_update_incrementally {
        rasterizer.render_delta(
            pixels,
            width,
            height,
            cells,
            &surface.rendered_cells,
            cols,
            rows,
            surface.scale,
        )?
    } else {
        rasterizer.render(
            pixels,
            width,
            height,
            cells,
            cols,
            rows,
            opacity,
            surface.scale,
        )?;
        Some(Damage {
            x: 0,
            y: 0,
            width,
            height,
        })
    };
    if damage.is_some() {
        surface.rendered_cells.resize(cells.len(), Cell::default());
        surface.rendered_cells.copy_from_slice(cells);
        surface.rendered_alpha = alpha;
        surface.rendered_grid = (cols, rows);
    }
    Ok(damage)
}

fn hash_cells(cells: &[Cell]) -> u64 {
    let mut hash = 0xcbf29ce484222325u64;
    for cell in cells {
        for byte in (cell.ch as u32).to_le_bytes() {
            hash ^= u64::from(byte);
            hash = hash.wrapping_mul(0x100000001b3);
        }
        for byte in cell.fg.into_iter().chain(cell.bg) {
            hash ^= u64::from(byte);
            hash = hash.wrapping_mul(0x100000001b3);
        }
    }
    hash
}

impl CompositorHandler for App {
    fn scale_factor_changed(
        &mut self,
        _: &Connection,
        qh: &QueueHandle<Self>,
        surface: &wl_surface::WlSurface,
        factor: i32,
    ) {
        if let Some(output) = self
            .surfaces
            .iter_mut()
            .find(|candidate| candidate.matches(surface))
        {
            output.scale = factor.max(1);
            output.layer.wl_surface().set_buffer_scale(output.scale);
            output.buffer = None;
            output.dirty = true;
        }
        if let Err(error) = self.ensure_animation() {
            self.failure = Some(error);
            self.exit = true;
        } else {
            self.present_dirty(qh, true);
        }
    }

    fn transform_changed(
        &mut self,
        _: &Connection,
        _: &QueueHandle<Self>,
        _: &wl_surface::WlSurface,
        _: wl_output::Transform,
    ) {
    }

    fn frame(
        &mut self,
        _: &Connection,
        qh: &QueueHandle<Self>,
        surface: &wl_surface::WlSurface,
        _: u32,
    ) {
        if let Some(output) = self
            .surfaces
            .iter_mut()
            .find(|candidate| candidate.matches(surface))
        {
            output.frame_ready = true;
        }
        self.present_dirty(qh, false);
    }

    fn surface_enter(
        &mut self,
        _: &Connection,
        _: &QueueHandle<Self>,
        _: &wl_surface::WlSurface,
        _: &wl_output::WlOutput,
    ) {
    }
    fn surface_leave(
        &mut self,
        _: &Connection,
        _: &QueueHandle<Self>,
        _: &wl_surface::WlSurface,
        _: &wl_output::WlOutput,
    ) {
    }
}

impl OutputHandler for App {
    fn output_state(&mut self) -> &mut OutputState {
        &mut self.output_state
    }
    fn new_output(&mut self, _: &Connection, qh: &QueueHandle<Self>, output: wl_output::WlOutput) {
        self.add_output(output, qh);
    }
    fn update_output(&mut self, _: &Connection, _: &QueueHandle<Self>, _: wl_output::WlOutput) {}
    fn output_destroyed(
        &mut self,
        _: &Connection,
        _: &QueueHandle<Self>,
        output: wl_output::WlOutput,
    ) {
        self.surfaces.retain(|surface| surface.output != output);
        if self.surfaces.is_empty() {
            self.exit = true;
        }
    }
}

impl LayerShellHandler for App {
    fn closed(&mut self, _: &Connection, _: &QueueHandle<Self>, _: &LayerSurface) {
        self.exit = true;
    }

    fn configure(
        &mut self,
        _: &Connection,
        qh: &QueueHandle<Self>,
        layer: &LayerSurface,
        configure: LayerSurfaceConfigure,
        _: u32,
    ) {
        let Some(surface) = self
            .surfaces
            .iter_mut()
            .find(|surface| surface.layer.wl_surface() == layer.wl_surface())
        else {
            return;
        };
        surface.width = NonZeroU32::new(configure.new_size.0)
            .map_or(surface.width.max(1), |value| value.get() as i32);
        surface.height = NonZeroU32::new(configure.new_size.1)
            .map_or(surface.height.max(1), |value| value.get() as i32);
        surface.configured = true;
        surface.dirty = true;
        surface.frame_ready = true;
        if let Err(error) = self.ensure_animation() {
            self.failure = Some(error);
            self.exit = true;
        } else {
            self.present_dirty(qh, true);
        }
    }
}

impl SeatHandler for App {
    fn seat_state(&mut self) -> &mut SeatState {
        &mut self.seat_state
    }
    fn new_seat(&mut self, _: &Connection, _: &QueueHandle<Self>, _: wl_seat::WlSeat) {}

    fn new_capability(
        &mut self,
        _: &Connection,
        qh: &QueueHandle<Self>,
        seat: wl_seat::WlSeat,
        capability: Capability,
    ) {
        if capability == Capability::Keyboard && self.keyboard.is_none() {
            match self.seat_state.get_keyboard(qh, &seat, None) {
                Ok(keyboard) => self.keyboard = Some(keyboard),
                Err(error) => self.failure = Some(anyhow!("creating keyboard: {error}")),
            }
        }
        if capability == Capability::Pointer && self.pointer.is_none() {
            match self.seat_state.get_pointer(qh, &seat) {
                Ok(pointer) => self.pointer = Some(pointer),
                Err(error) => self.failure = Some(anyhow!("creating pointer: {error}")),
            }
        }
    }

    fn remove_capability(
        &mut self,
        _: &Connection,
        _: &QueueHandle<Self>,
        _: wl_seat::WlSeat,
        capability: Capability,
    ) {
        if capability == Capability::Keyboard {
            self.keyboard.take();
        }
        if capability == Capability::Pointer {
            self.pointer.take();
        }
    }
    fn remove_seat(&mut self, _: &Connection, _: &QueueHandle<Self>, _: wl_seat::WlSeat) {}
}

impl KeyboardHandler for App {
    fn enter(
        &mut self,
        _: &Connection,
        _: &QueueHandle<Self>,
        _: &wl_keyboard::WlKeyboard,
        _: &wl_surface::WlSurface,
        _: u32,
        _: &[u32],
        _: &[Keysym],
    ) {
    }
    fn leave(
        &mut self,
        _: &Connection,
        _: &QueueHandle<Self>,
        _: &wl_keyboard::WlKeyboard,
        _: &wl_surface::WlSurface,
        _: u32,
    ) {
        // Hyprland transfers focus between per-output layer surfaces during startup.
        self.keyboard_transition(KeyboardTransition::FocusLeft);
    }
    fn press_key(
        &mut self,
        _: &Connection,
        _: &QueueHandle<Self>,
        _: &wl_keyboard::WlKeyboard,
        _: u32,
        _: KeyEvent,
    ) {
        self.keyboard_transition(KeyboardTransition::KeyPressed);
    }
    fn repeat_key(
        &mut self,
        _: &Connection,
        _: &QueueHandle<Self>,
        _: &wl_keyboard::WlKeyboard,
        _: u32,
        _: KeyEvent,
    ) {
    }
    fn release_key(
        &mut self,
        _: &Connection,
        _: &QueueHandle<Self>,
        _: &wl_keyboard::WlKeyboard,
        _: u32,
        _: KeyEvent,
    ) {
    }
    fn update_modifiers(
        &mut self,
        _: &Connection,
        _: &QueueHandle<Self>,
        _: &wl_keyboard::WlKeyboard,
        _: u32,
        _: Modifiers,
        _: RawModifiers,
        _: u32,
    ) {
    }
}

impl PointerHandler for App {
    fn pointer_frame(
        &mut self,
        _: &Connection,
        _: &QueueHandle<Self>,
        _: &wl_pointer::WlPointer,
        events: &[PointerEvent],
    ) {
        for event in events {
            if !self
                .surfaces
                .iter()
                .any(|surface| surface.matches(&event.surface))
            {
                continue;
            }
            match event.kind {
                PointerEventKind::Enter { .. } => self.last_pointer = Some(event.position),
                PointerEventKind::Motion { .. } => {
                    if self
                        .last_pointer
                        .is_some_and(|previous| previous != event.position)
                    {
                        self.request_dismiss();
                    }
                    self.last_pointer = Some(event.position);
                }
                PointerEventKind::Press { .. } => self.request_dismiss(),
                PointerEventKind::Leave { .. } => self.last_pointer = None,
                PointerEventKind::Release { .. } | PointerEventKind::Axis { .. } => {}
            }
        }
    }
}

impl ShmHandler for App {
    fn shm_state(&mut self) -> &mut Shm {
        &mut self.shm
    }
}

delegate_registry!(App);
impl ProvidesRegistryState for App {
    fn registry(&mut self) -> &mut RegistryState {
        &mut self.registry_state
    }
    registry_handlers![OutputState, SeatState];
}
smithay_client_toolkit::delegate_dispatch2!(App);

#[cfg(test)]
mod tests {
    use super::KeyboardTransition;

    #[test]
    fn output_focus_transfer_is_not_user_activity() {
        assert!(!KeyboardTransition::FocusLeft.dismisses());
        assert!(KeyboardTransition::KeyPressed.dismisses());
    }
}
