-- lute-lint-global-ignore(unused_variable)
local ui = {}

export type Signal<T> = {
	get: (self: Signal<T>) -> T,
	set: (self: Signal<T>, value: T) -> (),
}

export type Element = {
	read id: number,
}

export type Padding = number | {
	top: number?,
	right: number?,
	bottom: number?,
	left: number?,
}

export type KeyModifiers = {
	shift: boolean?,
	control: boolean?,
	alt: boolean?,
	meta: boolean?,
}

export type WindowProps = {
	title: string?,
	gap: number?,
	padding: Padding?,
	[number]: Element,
}

export type ButtonProps = {
	text: string | (() -> string)?,
	gap: number?,
	padding: Padding?,
	disabled: boolean?,
	onPress: (() -> ())?,
	onActivate: (() -> ())?,
	[number]: Element,
}

export type ContainerProps = {
	gap: number?,
	padding: Padding?,
	[number]: Element,
}

function ui.signal<T>(value: T): Signal<T>
	error("not implemented")
end

function ui.computed<T>(compute: () -> T): Signal<T>
	error("not implemented")
end

function ui.window(props: WindowProps): Element
	error("not implemented")
end

function ui.box(props: ContainerProps?): Element
	error("not implemented")
end

function ui.column(props: ContainerProps): Element
	error("not implemented")
end

function ui.row(props: ContainerProps): Element
	error("not implemented")
end

function ui.text(value: string | number | boolean | (() -> string | number | boolean)): Element
	error("not implemented")
end

function ui.button(props: ButtonProps): Element
	error("not implemented")
end

function ui.flush(root: Element?): ()
	error("not implemented")
end

function ui.activate(element: Element): boolean
	error("not implemented")
end

function ui.focus(element: Element): boolean
	error("not implemented")
end

function ui.focused(root: Element?): number?
	error("not implemented")
end

function ui.click(root: Element, x: number, y: number): boolean
	error("not implemented")
end

function ui.press_key(root: Element, key: string, modifiers: KeyModifiers?): boolean
	error("not implemented")
end

function ui.dump_ui_tree(root: Element?): string
	error("not implemented")
end

function ui.dump_layout_tree(root: Element?): string
	error("not implemented")
end

function ui.dump_scene(root: Element?): string
	error("not implemented")
end

function ui.dump_semantics(root: Element?): string
	error("not implemented")
end

function ui.dump_profile(root: Element?): string
	error("not implemented")
end

function ui.render(root: Element?): { backend: string, displayItemCount: number, sceneGeneration: number }
	error("not implemented")
end

--- Opens a native UI window for `root` and blocks until it closes.
function ui.run(root: Element?): ()
	error("not implemented")
end

return ui
