local ui = require("@lute/ui")

local count = ui.signal(0)

local root = ui.window {
	title = "Counter",

	ui.column {
		gap = 12,
		padding = 24,

		ui.text(function()
			return `🧮 Count: {count:get()}`
		end),

		ui.button {
			text = "Increment",
			onPress = function()
				count:set(count:get() + 1)
			end,
		},
	},
}

ui.run(root)

print(ui.dump_profile(root))
