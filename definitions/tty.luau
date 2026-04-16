export type WindowSize = {
	rows: number,
	cols: number,
}

local tty = {}

function tty.inputIsTTY(): boolean
	error("unimplemented")
end

function tty.outputIsTTY(): boolean
	error("unimplemented")
end

function tty.isRawMode(): boolean
	error("unimplemented")
end

function tty.setRawMode(enable: boolean): ()
	error("unimplemented")
end

function tty.getWindowSize(): WindowSize
	error("unimplemented")
end

function tty.read(): string
	error("unimplemented")
end

return tty
