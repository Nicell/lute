local json = require("@std/json")
local fs = require("@std/fs")
local time = require("@lute/time")

local CATEGORIES = {
	"electronics",
	"books",
	"toys",
	"clothing",
	"home",
	"garden",
	"music",
	"food",
}
local TAG_POOL = {
	"new",
	"sale",
	"popular",
	"limited",
	"organic",
	"imported",
	"vintage",
	"hot",
	"clearance",
	"premium",
}

local NAMES = {
	"Acme Widget",
	"Foobar Gadget",
	"Quux Doodad",
	"Plover Gizmo",
	"Wobble Sprocket",
	"Glimmer Trinket",
	"Whirly Whatsit",
	"Dingus Thingamajig",
	"Flummox Flapper",
	"Snickerdoodle",
}

local function buildDataset(count: number): { any }
	local out = table.create(count)
	for i = 1, count do
		local tags = table.create(3)
		tags[1] = TAG_POOL[((i * 3) % #TAG_POOL) + 1]
		tags[2] = TAG_POOL[((i * 7) % #TAG_POOL) + 1]
		tags[3] = TAG_POOL[((i * 11) % #TAG_POOL) + 1]
		out[i] = {
			id = i,
			name = NAMES[((i * 13) % #NAMES) + 1] .. " #" .. tostring(i),
			category = CATEGORIES[((i * 5) % #CATEGORIES) + 1],
			price = (i * 137) % 9999 / 100,
			quantity = (i * 17) % 50,
			active = (i % 3) == 0,
			tags = tags,
			rating = (i % 50) / 10,
		}
	end
	return out
end

local function buildJson(dataset: { any }, count: number, m: number): string
	local items = table.create(count)
	for i = 1, count do
		local src = dataset[i]
		items[i] = {
			id = src.id,
			name = src.name,
			category = src.category,
			price = src.price,
			quantity = src.quantity,
			active = src.active,
			tags = src.tags,
			rating = src.rating,
			total = src.price * src.quantity * m,
		}
	end
	return json.serialize({ count = count, items = items })
end

local function benchSerialize(label: string, datasetSize: number, iters: number)
	local dataset = buildDataset(datasetSize)

	-- warmup
	for _ = 1, 3 do
		buildJson(dataset, datasetSize, 1)
	end

	local start = time.now()
	local totalBytes = 0
	for i = 1, iters do
		local out = buildJson(dataset, datasetSize, i)
		totalBytes += #out
	end
	local elapsed = time.since(start)

	local perCall = elapsed / iters
	local mbPerSec = (totalBytes / 1024 / 1024) / elapsed
	print(
		string.format(
			"%-24s n=%4d  iters=%5d  total=%7.3fs  per-call=%9.3fms  throughput=%6.1f MB/s  bytes/call=%d",
			label,
			datasetSize,
			iters,
			elapsed,
			perCall * 1000,
			mbPerSec,
			math.floor(totalBytes / iters)
		)
	)
end

local function benchDeserialize(label: string, datasetSize: number, iters: number)
	local dataset = buildDataset(datasetSize)
	local payload = buildJson(dataset, datasetSize, 1)
	local payloadLen = #payload

	-- warmup
	for _ = 1, 3 do
		json.deserialize(payload)
	end

	local start = time.now()
	for _ = 1, iters do
		json.deserialize(payload)
	end
	local elapsed = time.since(start)

	local perCall = elapsed / iters
	local mbPerSec = (payloadLen * iters / 1024 / 1024) / elapsed
	print(
		string.format(
			"%-24s n=%4d  iters=%5d  total=%7.3fs  per-call=%9.3fms  throughput=%6.1f MB/s  bytes/call=%d",
			label,
			datasetSize,
			iters,
			elapsed,
			perCall * 1000,
			mbPerSec,
			payloadLen
		)
	)
end

print("=== json.serialize benchmark ===")
benchSerialize("tiny", 1, 50000)
benchSerialize("small", 10, 10000)
benchSerialize("medium", 100, 2000)
benchSerialize("large", 1000, 200)
benchSerialize("xlarge", 5000, 40)

print()
print("=== json.deserialize benchmark (synthetic, compact) ===")
benchDeserialize("tiny", 1, 50000)
benchDeserialize("small", 10, 10000)
benchDeserialize("medium", 100, 2000)
benchDeserialize("large", 1000, 200)
benchDeserialize("xlarge", 5000, 40)

-- Real-world payload from disk. Pretty-printed JSON exercises the
-- whitespace-skipping path differently than the synthetic compact output.
local function benchDeserializeFile(label: string, path: string, iters: number)
	local payload = fs.readFileToString(path)
	local payloadLen = #payload

	for _ = 1, 3 do
		json.deserialize(payload)
	end

	local start = time.now()
	for _ = 1, iters do
		json.deserialize(payload)
	end
	local elapsed = time.since(start)

	local mbPerSec = (payloadLen * iters / 1024 / 1024) / elapsed
	print(
		string.format(
			"%-24s        iters=%5d  total=%7.3fs  per-call=%9.3fms  throughput=%6.1f MB/s  bytes/call=%d",
			label,
			iters,
			elapsed,
			(elapsed / iters) * 1000,
			mbPerSec,
			payloadLen
		)
	)
end

print()
print("=== json.deserialize benchmark (real dataset.json, pretty-printed) ===")
benchDeserializeFile("dataset.json", "dataset.json", 5000)
print("=== json.deserialize benchmark (real dataset-large.json) ===")
benchDeserializeFile("dataset.json", "dataset-large.json", 50)
