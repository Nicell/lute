local ui = require("@lute/ui")

local args = { ... }

local function hasArg(name: string): boolean
	for _, value in args do
		if value == name then
			return true
		end
	end

	return false
end

local function numericArg(name: string): number?
	for i, value in args do
		local prefix = name .. "="
		if value:sub(1, #prefix) == prefix then
			return tonumber(value:sub(#prefix + 1))
		end

		if value == name and args[i + 1] ~= nil then
			return tonumber(args[i + 1])
		end
	end

	return nil
end

local headless = hasArg("--headless")
local stress = hasArg("--stress")
local dynamicRows = hasArg("--dynamic-rows")
local rows = numericArg("--rows")
local variant = ui.signal(0)

local paragraphs = {
	"System UI Latin: The quick brown fox jumps over 1234567890 while punctuation, quotes, and spacing stay aligned.",
	"Emoji fallback: 🧮 😀 😇 🧑🏽‍💻 👨‍👩‍👧‍👦 🇺🇸 🏳️‍🌈 should shape as whole grapheme clusters and use image glyph fallback.",
	"Combining marks: café, résumé, coöperate, Ångström should keep accents attached to their base letters.",
	"Precomposed forms: café, résumé, coöperate, Ångström should sit very close to the decomposed line above.",
	"Mixed bidi: English לפני עברית 123 وسط العربية 456 then back to English at the end.",
	"Fallback coverage probe: नमस्ते दुनिया สวัสดีโลก reveals which scripts resolve through platform fallback today.",
	"Wrapping paragraph: This sentence is intentionally long enough to wrap in the window. It exercises line break opportunities, paragraph layout, fragment emission, and repeated measurement through the shared TextShaper path.",
}

local function dynamicLine()
	local n = variant:get()
	if n % 3 == 0 then
		return "Dynamic update 0: Latin text plus 🧮 emoji fallback."
	elseif n % 3 == 1 then
		return "Dynamic update 1: English לפני עברית 123 then English."
	end

	return "Dynamic update 2: café 👨‍👩‍👧‍👦 wraps and remeasures cleanly."
end

local content: any = {
	gap = 10,
	padding = 24,
}

table.insert(content, ui.text("Text showcase"))
table.insert(content, ui.text(dynamicLine))
table.insert(content, ui.button {
	text = "Cycle text",
	onPress = function()
		variant:set(variant:get() + 1)
	end,
})

for _, text in paragraphs do
	table.insert(content, ui.text(text))
end

local repeatCount = rows or if stress then 18 else 4
for i = 1, repeatCount do
	local text = paragraphs[((i - 1) % #paragraphs) + 1]
	if dynamicRows then
		table.insert(content, ui.text(function()
			return `Row {i}.{variant:get()}: {text}`
		end))
	else
		table.insert(content, ui.text(`Row {i}: {text}`))
	end
end

local root = ui.window {
	title = "Text Showcase",

	ui.column(content),
}

if headless then
	ui.flush(root)
	variant:set(1)
	ui.flush(root)
	variant:set(2)
	ui.flush(root)
	print(ui.dump_profile(root))
else
	ui.run(root)
	print(ui.dump_profile(root))
end
