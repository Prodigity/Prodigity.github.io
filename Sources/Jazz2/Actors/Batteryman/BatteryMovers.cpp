#include "BatteryMovers.h"
#include "BatteryWorld.h"
#include "BatterymanPlayer.h"
#include "../../ContentResolver.h"

#include "../../../nCine/Graphics/RenderQueue.h"

#include <algorithm>

// Ports of the routines in teardown/42-objects-movers-hazards.md, from their decompiles (scratchpad g42/out);
// "+0xNN" is the object record, P the player. T is the tick counter _DAT_004b11c0, SW / TRIG the link tables
// 0x4d7dc0 / 0x4b11e0. Every behaviour runs after the player's physics and controller in the same tick, so what
// it writes into the player is integrated on the next tick.

namespace Jazz2::Actors::Batteryman
{
	namespace
	{
		/** @brief `FUN_004061a0` - the sign of a value, -1 / 0 / 1 */
		std::int32_t Sign(std::int32_t v) {
			return (v > 0 ? 1 : (v < 0 ? -1 : 0));
		}

		std::int32_t Abs(std::int32_t v) {
			return (v < 0 ? -v : v);
		}

		/** @brief Tile of a 16.16 coordinate as the probes take it, `v >> 21` (toward minus infinity) */
		std::int32_t TileOf(std::int32_t v) {
			return (v >> 21);
		}

		/** @brief Signed division by 2^n truncating toward zero, `(v + (v >> 31 & (2^n - 1))) >> n` */
		std::int32_t DivPow2(std::int32_t v, std::int32_t n) {
			return (v + ((v >> 31) & ((1 << n) - 1))) >> n;
		}

		std::uint16_t Anim(AnimSet set, std::int32_t anim) {
			return MakeAnimHandle((std::int32_t)set, anim);
		}

		constexpr std::int32_t One = 0x10000;
	}

	// ---- 0x23 DRIJVER ----------------------------------------------------------------------------------------

	void FloatRaft::Behave()
	{
		// LAB_00404fd0
		BatterymanPlayer* p = _world->GetPlayer();
		if (U8(0x41) == 0) {
			I32(0x50) = _world->GetEventParam(CellX(), CellY(), 0, 6);
			I32(0x5c) = 0;
			U8(0x41) = 0x1c;
		}
		U16(0x34) = Anim(AnimSet::Drijver, 0);
		// The water of the id; without one (yet) the pool's slot 0, all zeros
		BatteryObject* water = _world->GetObject(_world->WaterSlot(I32(0x50)));
		std::int32_t waterY = (water != nullptr ? water->I32(0x10) : 0);
		std::int32_t waterBottom = (water != nullptr ? water->I32(0x64) : 0);
		std::int32_t y = waterY - 0x80000;
		I32(0x10) = y;
		if (waterBottom - 0x100000 < y) {
			I32(0x5c) = 0;
			y = waterBottom - 0x100000;
		} else {
			// Sinks 0.25 px/tick up to 4 px while the player rides it, rises back as fast
			std::int32_t sink = I32(0x5c);
			if (p->RideSlot() == _slot) {
				I32(0x5c) = (sink < 0x40000 ? sink + 0x4000 : 0x40000);
			} else {
				I32(0x5c) = (sink < 0x4001 ? 0 : sink - 0x4000);
			}
			y = I32(0x10) + I32(0x5c);
		}
		I32(0x10) = y;
		U32(0x28) = FrameOf(U16(0x34), 0);
		DrawSelf();
		const FrameInfo* f = BatteryAnims::Get().GetFrame(U32(0x28));
		_world->TopPlatform(this, (f != nullptr ? (std::uint16_t)f->Width : 0), 0x2c, 2);
	}

	// ---- 0x25 / 0x29 pump ------------------------------------------------------------------------------------

	void Pump::Behave()
	{
		// LAB_00405100
		if (U8(0x41) == 0) {
			std::int32_t tx = CellX(), ty = CellY();
			I32(0x50) = _world->GetLinkId(tx, ty);
			I32(0x54) = _world->GetEventParam(tx, ty, 6, 6);
			I32(0x58) = _world->GetEventParam(tx, ty, 12, 6);
			if (I32(0x54) == I32(0x58)) {
				LOGW("pomp at {},{} heeft 2x zelfde water id", tx, ty);
			}
			U8(0x41) = 0x1c;
			I32(0x64) = 0;
			I32(0x60) = _world->ActivationCount(I32(0x50));
		}
		// A missing water id reads (and here leaves alone) the empty slot 0
		BatteryObject* a = _world->GetObject(_world->WaterSlot(I32(0x54)));
		BatteryObject* b = _world->GetObject(_world->WaterSlot(I32(0x58)));
		std::int32_t id = I32(0x50);
		if ((std::uint32_t)_world->ActivationCount(id) != (std::uint32_t)I32(0x60)) {
			// Every switch event turns both waters around: 1 = fill to the top, 0 = drain
			if (a != nullptr) {
				a->I32(0x5c) = (a->I32(0x5c) == 0 ? 1 : 0);
			}
			if (b != nullptr) {
				b->I32(0x5c) = (b->I32(0x5c) == 0 ? 1 : 0);
			}
			I32(0x64) = 0x80;
			I32(0x60) = _world->ActivationCount(id);
			LOGI("[Pump] tick {} slot {} switched: water {} → {}, water {} → {}", _world->GetPlayer()->GetTickCount(), _slot,
				I32(0x54), (a != nullptr ? a->I32(0x5c) : 0), I32(0x58), (b != nullptr ? b->I32(0x5c) : 0));
		}
		// The pump ages its own id's activity timer too, 1 per tick
		if (_world->Trigger(id) != 0) {
			_world->Trigger(id)--;
		}
		if (I32(0x64) < 1) {
			return;
		}

		// 1 px per tick each until both are at their end
		auto step = [](BatteryObject* w) -> bool {
			if (w == nullptr) {
				return true;
			}
			std::int32_t level = w->I32(0x6c);
			if (w->I32(0x5c) == 0) {
				if (level < 1) {
					return true;
				}
				w->I32(0x6c) = level - 1;
			} else {
				if (w->I32(0x58) <= level) {
					return true;
				}
				w->I32(0x6c) = level + 1;
			}
			return false;
		};
		bool doneA = step(a);
		bool doneB = step(b);
		if (doneA && doneB) {
			I32(0x64) = 0;
		} else {
			I32(0x64) = 1;
			HoldSample(I32(0x0c), I32(0x10), 0x39, 0x80, 0);
		}
		if (!doneA) {
			a->HoldSample(a->I32(0x0c), a->I32(0x10), 0x3a, 0x80, 0);
		}
		if (!doneB) {
			b->HoldSample(b->I32(0x0c), b->I32(0x10), 0x3a, 0x80, 0);
		}
	}

	// ---- 0x2e water ------------------------------------------------------------------------------------------

	void WaterVolume::Behave()
	{
		// LAB_00404c60
		BatterymanPlayer* p = _world->GetPlayer();
		std::uint32_t t = p->GetTickCount();
		if (U8(0x41) == 0) {
			std::int32_t tx = CellX(), ty = CellY();
			I32(0x0c) -= 0xf0000;
			I32(0x50) = _world->GetEventParam(tx, ty, 0, 6);
			I32(0x54) = _world->GetEventParam(tx, ty, 6, 5) << 5;
			I32(0x58) = _world->GetEventParam(tx, ty, 11, 5) * 0x20 + 0x10;
			I32(0x5c) = _world->GetEventParam(tx, ty, 16, 1);
			I32(0x60) = _world->GetEventParam(tx, ty, 17, 1);
			I32(0x64) = (I32(0x58) - 0x1f) * 0x10000 + I32(0x10);
			I32(0x6c) = (I32(0x5c) == 0 ? 0 : I32(0x58));
			if (_world->WaterSlot(I32(0x50)) != 0 && I32(0x50) != 0) {
				LOGW("Water met ID {} bestaat al", I32(0x50));
			}
			_world->WaterSlot(I32(0x50)) = (std::uint16_t)_slot;
			U8(0x41) = 1;
		}

		if (_world->IsDrawingTick()) {
			_tintQueued = false;
		}
		std::int32_t level = I32(0x6c);
		if (level < 1) {
			I32(0x68) = 0;
			I32(0x10) = I32(0x64);
		} else {
			// The surface waves 0..16 px with a period of 256 ticks
			std::int32_t wave = DivPow2(BatteryWorld::Sine((std::int32_t)((t * 4) & 0x3ff)), 13) + 8;
			std::int32_t depth = level - wave;
			std::int32_t y = (wave - level) * 0x10000 + I32(0x64);
			I32(0x10) = y;
			I32(0x68) = depth;
			if (0 < depth) {
				if (_world->IsDrawingTick()) {
					// FUN_0040e210(X, Y, 4, 4, slot, 0): the tint entry, in view pixels
					_tintQueued = true;
					_tintX = DivPow2(I32(0x0c), 16) - DivPow2(p->GetCameraX16(), 16);
					_tintY = DivPow2(y, 16) - DivPow2(p->GetCameraY16(), 16);
				}
				if ((t & 7) == 0) {
					std::int32_t pX = p->X16(), pY = p->Y16();
					if (I32(0x0c) - 0x1400000 < pX && pX < (I32(0x54) + 0x140) * 0x10000 + I32(0x0c) &&
						I32(0x10) - 0x1900000 < pY && pY < (I32(0x68) + 400) * 0x10000 + I32(0x10)) {
						std::uint32_t r = _world->Random();
						_world->SpawnBubble(I32(0x0c) + 0x80000 + (std::int32_t)(r & 0x7fff) * (I32(0x54) - 0x10) * 2,
							(I32(0x68) - 8) * 0x10000 + I32(0x10), (std::int8_t)I32(0x50));
					}
				}
			}
		}

		// In the water (the anchor less than 32 px above the surface) or just above it (less than 128 px)
		std::int32_t pX = p->X16(), pY = p->Y16();
		if (0 < I32(0x6c) && I32(0x0c) < pX && pX < I32(0x54) * 0x10000 + I32(0x0c)) {
			std::int32_t y = I32(0x10);
			if (y < pY + 0x800000 && pY < I32(0x68) * 0x10000 + y) {
				if (y < pY + 0x200000) {
					if (p->InWaterSlot() != _slot) {
						PlaySample(0, 0, 0x16, 0, 0);
					}
					p->InWaterSlot() = _slot;
					p->WaterBelowSlot() = _slot;
					return;
				}
				if (p->InWaterSlot() == _slot) {
					p->InWaterSlot() = 0;
				}
				p->WaterBelowSlot() = _slot;
				return;
			}
		}
		if (p->InWaterSlot() == _slot) {
			p->InWaterSlot() = 0;
		}
		if (p->WaterBelowSlot() == _slot) {
			p->WaterBelowSlot() = 0;
		}
	}

	bool WaterVolume::OnDraw(RenderQueue& renderQueue)
	{
		if (!_tintQueued || _world == nullptr) {
			return true;
		}
		std::int32_t used = 0;
		DrawTint(renderQueue, used, 0);
		DrawTint(renderQueue, used, 1);
		return true;
	}

	void WaterVolume::DrawTint(RenderQueue& renderQueue, std::int32_t& used, std::int32_t pass)
	{
		// FUN_0041ad70(sx, sy, slot, pass): rows from 16 px above the surface down to the bottom, each a span of the
		// view remapped through one row of the blend table - the first 32 rows (the surface band, tapered at the
		// edges by +0x60) through DAT_004dac4c, the rest through DAT_004c5420. Pass 0 (before the sprites) takes
		// the rows above the surface line, pass 1 (after the particles) the rest.
		constexpr std::int32_t W = BatterymanPlayer::ViewWidth, H = BatterymanPlayer::ViewHeight;
		std::int32_t sx = _tintX, sy = _tintY;
		std::int32_t top = 0, height = H;
		if (H <= sy) {
			return;
		}
		if (pass == 0) {
			if (sy < 1) {
				return;
			}
			height = sy;
		} else if (0 < sy) {
			height = H - sy;
			top = sy;
			sy = 0;
		}
		std::int32_t start = sy - 0x10;
		if (!(start < height)) {
			return;
		}
		std::int32_t w = I32(0x54);
		std::int32_t rows = I32(0x68) + 0x10;
		if (!(sx <= W && -1 < sx + w + 0x28)) {
			return;
		}

		// The taper: row k of the band starts 39 - 1.25·k px in (left edge) and is cut on the right by the shape
		std::uint32_t edge = U32(0x60);
		std::int32_t limitRow = 0, limitWidth = 0;
		if ((edge & 3) == 0) {
			std::int32_t v = (I32(0x58) * 0x10000 - I32(0x64)) + I32(0x10);
			limitRow = 0x1f - DivPow2(v, 16);
			limitWidth = w - v / 0xcccc;
		}
		std::int32_t taperX[32], taperW[32];
		for (std::int32_t k = 0; k < 32; k++) {
			std::int32_t in = 0x27 - DivPow2(k * 0x28, 5);
			taperX[k] = ((std::int32_t)edge < 4 ? sx + in : sx);
			if ((edge & 3) == 1) {
				taperW[k] = w - in;
			} else if ((edge & 3) == 2) {
				taperW[k] = w;
			} else {
				taperW[k] = (limitRow < k ? w - in : limitWidth);
			}
		}

		if (height <= rows + start) {
			rows = height - start;
		}
		std::int32_t k = 0;
		std::int32_t row = top;
		if (start < 0) {
			k = -start;
		} else {
			row = top + start;
		}
		std::uint16_t layer = BatteryWorld::GetTintRenderLayer(pass);
		std::uint16_t order = (std::uint16_t)_slot;
		// The body rows are all alike: drawn as one span per run
		std::int32_t runX = 0, runY = 0, runW = 0, runH = 0;
		for (; k < rows; k++) {
			if (k < 32) {
				std::int32_t x = taperX[k], width = taperW[k];
				if (x < W && 0 < width + x && 0 < width) {
					if (x < 0) {
						width += x;
						x = 0;
					}
					if (W < width + x) {
						width = W - x;
					}
					DrawSpan(renderQueue, used, x, row, width, BatteryWorld::WaterSurfaceRemap(), layer, order);
					row++;
				}
				// A row of the band entirely outside the view does not advance the destination (the original's)
			} else {
				if (W <= sx || sx + w < 1) {
					break;
				}
				std::int32_t x = sx, width = w;
				if (sx < 0) {
					x = 0;
					width = sx + w;
				}
				if (W < width + x) {
					width = W - x;
				}
				if (runH > 0 && runX == x && runW == width && runY + runH == row) {
					runH++;
				} else {
					if (runH > 0) {
						DrawSpan(renderQueue, used, runX, runY, runW, BatteryWorld::WaterBodyRemap(), layer, order);
						_spanCommands[used - 1]->GetInstanceBlock()->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue((float)runW, (float)runH);
					}
					runX = x; runY = row; runW = width; runH = 1;
				}
				row++;
			}
		}
		if (runH > 0) {
			DrawSpan(renderQueue, used, runX, runY, runW, BatteryWorld::WaterBodyRemap(), layer, order);
			if (used > 0) {
				_spanCommands[used - 1]->GetInstanceBlock()->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue((float)runW, (float)runH);
			}
		}
	}

	void WaterVolume::DrawSpan(RenderQueue& renderQueue, std::int32_t& used, std::int32_t x, std::int32_t y, std::int32_t width,
		std::uint8_t remap, std::uint16_t layer, std::uint16_t order)
	{
		// The blend table FUN_00411180: rows 0 and 1 keep the pixel; 0x10..0xef are the nearest colour to
		// (2·row + pixel) / 3; 0xf0..0xf7 brighten, (7·pixel + 256 + 32·k) / 8; row 0x0f and the unbuilt rows map
		// every pixel to one colour (0x0f, else 0)
		if (remap < 2 || width <= 0) {
			return;
		}
		if (used >= (std::int32_t)_spanCommands.size()) {
			auto& command = _spanCommands.emplace_back(std::make_unique<RenderCommand>(RenderCommand::Type::Sprite));
			command->GetMaterial().SetShaderProgramType(Material::ShaderProgramType::SpriteNoTexture);
			command->GetMaterial().SetBlendingEnabled(true);
			command->GetMaterial().ReserveUniformsDataMemory();
			command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);
		}
		auto* command = _spanCommands[used++].get();
		auto palette = ContentResolver::Get().GetPalettes();
		auto toColor = [](std::uint32_t rgba, float alpha) {
			return Colorf((float)(rgba & 0xff) / 255.0f, (float)((rgba >> 8) & 0xff) / 255.0f, (float)((rgba >> 16) & 0xff) / 255.0f, alpha);
		};
		Colorf color;
		if (remap >= 0x10 && remap <= 0xef) {
			command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);
			color = toColor(palette[remap], 2.0f / 3.0f);
		} else if (remap >= 0xf0 && remap <= 0xf7) {
			command->GetMaterial().SetBlendingFactors(BlendingFactor::One, BlendingFactor::OneMinusSrcAlpha);
			float add = std::min((256.0f + 32.0f * (float)(remap - 0xf0)) / 8.0f / 255.0f, 1.0f);
			color = Colorf(add, add, add, 1.0f / 8.0f);
		} else {
			command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);
			color = toColor(palette[remap == 0x0f ? 0x0f : 0], 1.0f);
		}
		BatterymanPlayer* p = _world->GetPlayer();
		std::int32_t viewX = DivPow2(p->GetCameraX16(), 16), viewY = DivPow2(p->GetCameraY16(), 16);
		auto* instanceBlock = command->GetInstanceBlock();
		instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue((float)width, 1.0f);
		instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(color.Data());
		command->SetTransformation(Matrix4x4f::Translation((float)(viewX + x), (float)(viewY + y), 0.0f));
		command->SetLayer(layer);
		command->SetVisitOrder(order);
		renderQueue.AddCommand(command);
	}

	// ---- 0x26 LIFT -------------------------------------------------------------------------------------------

	void Lift::Behave()
	{
		// LAB_00406530
		BatterymanPlayer* p = _world->GetPlayer();
		std::uint32_t t = p->GetTickCount();
		if (U8(0x41) == 0) {
			std::int32_t tx = CellX(), ty = CellY();
			I32(0x50) = _world->GetLinkId(tx, ty);
			I32(0x54) = _world->GetEventParam(tx, ty, 6, 3);
			I32(0x58) = _world->GetEventParam(tx, ty, 9, 1) * 2 - 1;
			I32(0x5c) = _world->GetEventParam(tx, ty, 10, 4) << 16;
			I32(0x54) += _world->GetEventParam(tx, ty, 14, 3) * 8;
			I32(0x6c) = I32(0x10);
			I32(0x18) = 0;
			I32(0x60) = _world->ActivationCount(I32(0x50));
			I32(0x14) = 0;
			I32(0x70) = 0;
			U8(0x41) = 0x1c;
		}
		if ((std::uint32_t)_world->ActivationCount(I32(0x50)) != (std::uint32_t)I32(0x60)) {
			// Every switch event starts it again in its configured direction
			U8(0x41) = 8;
			I32(0x60) = _world->ActivationCount(I32(0x50));
			I32(0x20) = I32(0x58) << 11;
			I32(0x18) = 0;
			I32(0x64) = 0;
			LOGI("[Lift] tick {} slot {} started, direction {}", t, _slot, I32(0x58));
		}
		if (U8(0x41) == 8) {
			if (Sign(I32(0x18)) == Sign(I32(0x20))) {
				I32(0x70) = 0;
				I32(0x18) += I32(0x20);
			} else {
				I32(0x18) += I32(0x20) * 0x10;
			}
			std::int32_t m = I32(0x5c);
			if (m < I32(0x18) || (m = -m, I32(0x18) < m)) {
				I32(0x18) = m;
			}
			std::int32_t vy = I32(0x18);
			I32(0x6c) += vy;
			HoldSample(I32(0x0c), I32(0x10), 0xd, 0x80, Abs(vy) >> 5);
			if (I32(0x70) == 0) {
				// Reverses at a 0x27 marker or a wall 80 px ahead
				std::int32_t x = I32(0x0c);
				std::int32_t y = (I32(0x18) < 0 ? I32(0x10) - 0x500000 : I32(0x10) + 0x500000);
				if (_world->GetEventType(TileOf(x), TileOf(y)) == 0x27 || _world->TestPixel(x, y) != 0) {
					std::int32_t a = Abs(I32(0x20));
					I32(0x20) = (I32(0x18) < 1 ? a : -a);
					I32(0x70) = 1;
					if (I32(0x54) <= ++I32(0x64)) {
						U8(0x41) = 0x1c;
						I32(0x64) = 0;
					}
				}
			}
			I32(0x10) = I32(0x6c);
		} else if (U8(0x41) == 0x1c) {
			if (I32(0x70) == 0 || Sign(I32(0x18)) == Sign(I32(0x20))) {
				I32(0x70) = 0;
				I32(0x18) = 0;
			} else {
				I32(0x18) += I32(0x20) * 0x10;
			}
			I32(0x6c) += I32(0x18);
			I32(0x10) = I32(0x6c) + BatteryWorld::Sine((std::int32_t)((t * 8) & 0x3ff)) * 4;
		}
		U16(0x34) = Anim(AnimSet::Lift, 0);
		U32(0x28) = FrameOf(U16(0x34), 0);
		DrawSprite(I32(0x0c), I32(0x10), 3, 0, 0, 0, U32(0x28));
		if ((t & 0x1f) < 0xf) {
			U32(0x28) = FrameOf(U16(0x34), 1);
			DrawSprite(I32(0x0c), I32(0x10), 3, 0, 0, 0, U32(0x28));
		}
		DrawSprite(I32(0x0c), I32(0x10), 3, 0, 9, 0, FrameOf(U16(0x34), 2));
		_world->SolidBox(this, I32(0x0c) - 0x140000, I32(0x10), 0x28, 0x10, 2);
	}

	// ---- 0x28 PLATFORM ---------------------------------------------------------------------------------------

	void SwayPlatform::Behave()
	{
		// LAB_004061c0
		BatterymanPlayer* p = _world->GetPlayer();
		std::uint32_t t = p->GetTickCount();
		if (U8(0x41) == 0) {
			std::int32_t tx = CellX(), ty = CellY();
			I32(0x50) = _world->GetLinkId(tx, ty);
			I32(0x54) = _world->GetEventParam(tx, ty, 6, 3);
			I32(0x58) = _world->GetEventParam(tx, ty, 9, 1) * 2 - 1;
			I32(0x5c) = _world->GetEventParam(tx, ty, 10, 4) << 16;
			I32(0x54) += _world->GetEventParam(tx, ty, 14, 3) * 8;
			I32(0x6c) = I32(0x10);
			I32(0x18) = 0;
			I32(0x60) = _world->ActivationCount(I32(0x50));
			I32(0x14) = 0;
			I32(0x68) = 0x24;
			U8(0x41) = 0x1c;
			I32(0x6c) = I32(0x10);
			I32(0x70) = 0;
		}
		if ((std::uint32_t)_world->ActivationCount(I32(0x50)) != (std::uint32_t)I32(0x60)) {
			U8(0x41) = 8;
			I32(0x60) = _world->ActivationCount(I32(0x50));
			I32(0x1c) = I32(0x58) << 11;
			I32(0x14) = 0;
			I32(0x64) = 0;
			LOGI("[Platform] tick {} slot {} started, direction {}", t, _slot, I32(0x58));
		}
		if (U8(0x41) == 8) {
			if (Sign(I32(0x14)) == Sign(I32(0x1c))) {
				I32(0x70) = 0;
				I32(0x14) += I32(0x1c);
			} else {
				I32(0x14) += I32(0x1c) * 8;
			}
			std::int32_t m = I32(0x5c);
			if (m < I32(0x14) || (m = -m, I32(0x14) < m)) {
				I32(0x14) = m;
			}
			std::int32_t vx = I32(0x14);
			I32(0x0c) += vx;
			HoldSample(I32(0x0c), I32(0x10), 0x38, 0x80, (Abs(vx) >> 6) + 8000);
			if (I32(0x70) == 0) {
				std::int32_t x = (I32(0x14) < 0 ? I32(0x0c) - 0x500000 : I32(0x0c) + 0x500000);
				std::int32_t y = I32(0x08);
				if (_world->GetEventType(TileOf(x), TileOf(y)) == 0x27 || _world->TestPixel(x, y) != 0) {
					std::int32_t a = Abs(I32(0x1c));
					I32(0x1c) = (I32(0x14) < 1 ? a : -a);
					I32(0x70) = 1;
					if (I32(0x54) <= ++I32(0x64)) {
						U8(0x41) = 0x1c;
						I32(0x64) = 0;
					}
				}
			}
		} else {
			if (I32(0x70) == 0 || Sign(I32(0x14)) == Sign(I32(0x1c))) {
				I32(0x70) = 0;
				I32(0x14) = 0;
			} else {
				I32(0x14) += I32(0x1c) * 0x10;
			}
			I32(0x0c) += I32(0x14);
		}
		I32(0x10) = I32(0x6c) + BatteryWorld::Sine((std::int32_t)((t * 8) & 0x3ff)) * 4;
		// Tilts one step per tick with the motion, 0x24..0x44
		if (I32(0x14) < 0x1001) {
			if (!(-0x1001 < I32(0x14)) && !(0x43 < I32(0x68))) {
				I32(0x68)++;
			}
		} else if (0x24 < I32(0x68)) {
			I32(0x68)--;
		}
		U16(0x34) = Anim(AnimSet::Platform, 0);
		std::int32_t tilt = DivPow2(I32(0x68), 2);
		U32(0x28) = FrameOf(U16(0x34), tilt - 9);
		DrawSprite(I32(0x0c), I32(0x10), 3, 0, 0, 0, U32(0x28));
		DrawSprite(I32(0x0c), I32(0x10), 3, 0, 9, 0, FrameOf(U16(0x34), tilt));
		_world->SolidBox(this, I32(0x0c) - 0x180000, I32(0x10), 0x2c, 0x10, 2);
	}

	// ---- 0x2b STAMPER / 0x2c ZWARESTAMPER --------------------------------------------------------------------

	void Stamper::Behave()
	{
		// LAB_00405db0 (STAMPER) / LAB_004029f0 (ZWARESTAMPER): one cycle of the phase +0x24 is 0..0x3ff, down
		// on 0..0x1ff and back up; +0x6c is what is left of the run, +0x60 cycles per switch event
		BatterymanPlayer* p = _world->GetPlayer();
		std::uint32_t t = p->GetTickCount();
		std::int32_t step = (_heavy ? 10000 : 15000);
		std::int32_t topOffset = (_heavy ? -0x500000 : -0x780000);
		std::int32_t riseOffset = (_heavy ? 0x4c4000 : 0x726000);
		auto phaseY = [&](std::int32_t ph) {
			return (ph < 0x200 ? I32(0x08) + topOffset + ph * step : I32(0x08) + riseOffset - ph * step);
		};

		if (U8(0x41) == 0) {
			std::int32_t tx = CellX(), ty = CellY();
			I32(0x50) = _world->GetLinkId(tx, ty);
			I32(0x54) = _world->GetEventParam(tx, ty, 6, 1) << 9;
			I32(0x58) = _world->GetEventParam(tx, ty, 8, 1) << 9;
			I32(0x5c) = _world->GetEventParam(tx, ty, 9, 3) << 1;
			I32(0x60) = _world->GetEventParam(tx, ty, 12, 6);
			I32(0x64) = 0;
			U16(0x34) = Anim(_heavy ? AnimSet::ZwareStamper : AnimSet::Stamper, 0);
			I32(0x24) = I32(0x54);
			U8(0x41) = 0x1c;
			I32(0x10) = phaseY(I32(0x24));
			I32(0x70) = _world->ActivationCount(I32(0x50));
		}
		U32(0x28) = FrameOf(U16(0x34), 2);
		if ((std::uint32_t)I32(0x70) != (std::uint32_t)_world->ActivationCount(I32(0x50))) {
			I32(0x70) = _world->ActivationCount(I32(0x50));
			I32(0x6c) = (I32(0x60) * 0x400 - I32(0x58)) + I32(0x24);
			LOGI("[Stamper] tick {} slot {} started: {} cycles from phase {}", t, _slot, I32(0x60), I32(0x24));
		}

		bool moved = false;
		if (0 < I32(0x6c)) {
			HoldSample(I32(0x0c), I32(0x10), 0x37, 0, 0);
			std::int32_t speed = I32(0x5c);
			I32(0x6c) -= Abs(speed);
			if (I32(0x6c) < 0) {
				I32(0x24) = I32(0x58);
			} else {
				I32(0x24) = (I32(0x24) + speed) & 0x3ff;
			}
			// At the top of each cycle a rider is thrown up
			if (I32(0x24) < speed && p->RideSlot() == _slot) {
				p->Y16() -= 0x40000;
				p->VY16() = -0x80000;
				if (p->TransporterTicks() == 0 && p->GrabbedBySlot() == 0) {
					p->SetPoseFromObject((p->GetInputH() & 0xf) == 0 ? 0x49 : 0x2b, 0);
				}
			}
			std::int32_t ph = I32(0x24);
			if (0x1ff < ph) {
				I32(0x18) = -0x40000;
				I32(0x10) = phaseY(ph);
				moved = true;
			} else {
				I32(0x10) = phaseY(ph);
				if (0x1e0 < ph && I32(0x64) == 0) {
					I32(0x64) = -1;
				}
				if (200 < ph) {
					// The dangerous half of the way down
					I32(0x18) = 0x40000;
					CrushSuckers();
					moved = true;
				}
			}
		}
		if (!moved) {
			I32(0x18) = 0;
		}

		// A crushed player's pose frames follow the stamper
		std::int32_t cap = (_heavy ? 4 : 6);
		if (p->GrabbedBySlot() == _slot && p->RideSlot() != _slot && I32(0x24) < 400 && (std::int8_t)p->Frame() < cap) {
			p->Frame() = (I32(0x24) * (_heavy ? 2 : 3)) / 200;
		}

		// The squash on impact: frames 2, 1, 0, 1, 2
		std::int32_t c = I32(0x64), f;
		if (c < 0) {
			c--;
			I32(0x64) = c;
			std::int32_t q = DivPow2(c, 2);
			f = q + 2;
			if (f < 1) {
				f = -2 - q;
			}
			if (2 < f) {
				f = 2;
				I32(0x64) = 0;
			}
		} else if (c < 1) {
			f = 2;
		} else {
			c++;
			I32(0x64) = c;
			f = DivPow2(c, 2) + 2;
			if (4 < f) {
				f = 8 - f;
			}
			if (f < 2) {
				f = 2;
				I32(0x64) = 0;
			}
		}
		U32(0x28) = FrameOf(U16(0x34), f);
		DrawSprite(I32(0x0c), I32(0x10), 3, 0, 0, 0, U32(0x28));
		if (_heavy) {
			DrawSprite(I32(0x04), I32(0x08) - 0xa0000, 3, 0, 9, 0, FrameOf(U16(0x34), 7));
			_world->SolidBox(this, I32(0x0c) - 0x190000, I32(0x10) - 0x250000, 0x32, 100, 1);
		} else {
			DrawSprite(I32(0x04), I32(0x08), 3, 0, 9, 0, FrameOf(U16(0x34), 6));
			if ((t & 0x1f) < 0xf) {
				DrawSprite(I32(0x04), I32(0x08), 3, 0, 9, 0, FrameOf(U16(0x34), 7));
			}
			_world->SolidBox(this, I32(0x0c) - 0x160000, I32(0x10) - 0x140000, 0x32, 0x78, 1);
		}
	}

	void Stamper::CrushSuckers()
	{
		// FUN_00402940
		for (std::int32_t i = 1; i < _world->GetLiveEnd(); i++) {
			BatteryObject* o = _world->GetObject(i);
			if (o == nullptr || (o->U8(0x44) != 0x33 && o->U8(0x44) != 0x34)) {
				continue;
			}
			std::int32_t y = o->I32(0x10);
			if (I32(0x10) + 0x400000 < y && y < I32(0x10) + 0x640000) {
				std::int32_t x = o->I32(0x0c);
				if (I32(0x0c) - 0x100000 < x && x < I32(0x0c) + 0x100000) {
					_world->PlayWorldSample(x, y, 0x26, 100, 0);
					_world->Shatter(o, 2);
					_world->Destroy(i);
					LOGI("[Stamper] tick {} slot {} crushed sucker {}", _world->GetPlayer()->GetTickCount(), _slot, i);
				}
			}
		}
	}

	// ---- 0x2f BAND -------------------------------------------------------------------------------------------

	void ConveyorBelt::Behave()
	{
		// FUN_00405aa0
		BatterymanPlayer* p = _world->GetPlayer();
		if (U8(0x41) == 0) {
			std::int32_t tx = CellX(), ty = CellY();
			I32(0x50) = _world->GetLinkId(tx, ty);
			I32(0x14) = _world->GetEventParam(tx, ty, 6, -5);
			I32(0x54) = _world->GetEventParam(tx, ty, 11, 4);
			U8(0x41) = 0x1c;
			I32(0x5c) = _world->GetEventParam(tx, ty, 15, 5) * 0x46;
			I32(0x10) -= 0x70000;
			I32(0x60) = (I32(0x54) + 2) * 0x40;
			I32(0x64) = _world->ActivationCount(I32(0x50));
		}
		if ((std::uint32_t)I32(0x64) != (std::uint32_t)_world->ActivationCount(I32(0x50))) {
			I32(0x64) = _world->ActivationCount(I32(0x50));
			I32(0x68) = I32(0x5c);
			LOGI("[Band] tick {} slot {} runs for {} ticks at {} px/tick", p->GetTickCount(), _slot, I32(0x5c), I32(0x14));
		}
		std::int32_t speed = 0;
		if (0 < I32(0x68)) {
			// Spins up over the first 128 ticks and down over the last 128, the sound's rate with it
			std::int32_t left = --I32(0x68);
			std::int32_t freq;
			if (left < 0x80) {
				speed = DivPow2(I32(0x14) * left, 7);
				freq = DivPow2(left * 0x5622, 7);
			} else if (I32(0x5c) - 0x80 < left) {
				std::int32_t e = I32(0x5c) - left;
				speed = DivPow2(I32(0x14) * e, 7);
				freq = DivPow2(e * 0x5622, 7);
			} else {
				speed = I32(0x14);
				freq = 0x5622;
			}
			if (p->RideSlot() == _slot) {
				HoldSample(0, 0, 8, 0x40, freq);
			} else {
				HoldSample(I32(0x0c), I32(0x10), 8, 0x80, freq);
			}
			if (speed != 0) {
				I32(0x24) -= speed;
				if (I32(0x24) < 0) {
					I32(0x24) += 0x189c;
				}
			}
		}
		std::int32_t x = I32(0x0c);
		std::int32_t y = I32(0x10) - 0x80000;
		std::int8_t f = (std::int8_t)((I32(0x24) / 7) % 3);
		U8(0x3c) = (std::uint8_t)f;
		U16(0x34) = Anim(AnimSet::Band, 0);
		U32(0x28) = FrameOf(U16(0x34), f);
		DrawSprite(x - 0xf0000, y, 3, 0, 0, 0, U32(0x28));
		x += 0x2f0000;
		for (std::int32_t k = 0; k < I32(0x54); k++) {
			U16(0x34) = Anim(AnimSet::Band, 1);
			U32(0x28) = FrameOf(U16(0x34), f);
			DrawSprite(x, y, 3, 0, 0, 0, U32(0x28));
			x += 0x3f0000;
		}
		U16(0x34) = Anim(AnimSet::Band, 2);
		U32(0x28) = FrameOf(U16(0x34), f);
		DrawSprite(x, y, 3, 0, 0, 0, U32(0x28));
		_world->SolidBox(this, I32(0x0c), I32(0x10), I32(0x60) - 0x10, 0xe, 1);
		if (p->RideSlot() == _slot && 0 < I32(0x68)) {
			p->ExtVX() = speed << 16;
		}
	}

	// ---- 0x30 ZUIGER -----------------------------------------------------------------------------------------

	void SuctionTube::Behave()
	{
		// LAB_00405310
		BatterymanPlayer* p = _world->GetPlayer();
		if (U8(0x41) == 0) {
			std::int32_t tx = CellX(), ty = CellY();
			I32(0x0c) -= 0xf0000;
			I32(0x10) += 0xf0000;
			I32(0x50) = _world->GetLinkId(tx, ty);
			I32(0x54) = (_world->GetEventParam(tx, ty, 6, 4) + 2) * 0x20;
			I32(0x58) = _world->GetEventParam(tx, ty, 10, 4) * 0x46;
			U8(0x41) = 0x1c;
			I32(0x5c) = _world->GetEventParam(tx, ty, 14, 4) + 4;
		} else if (U8(0x41) == 0x1c) {
			// The player's own sprite replaces the top while it is sucked out (pose 0x3a) and the mouth while
			// it is sucked in (0x3b) - for every tube, by the player's pose alone
			if (p->GetLatchedPose() != 0x3a) {
				U16(0x34) = Anim(AnimSet::Zuiger, 0);
				U32(0x28) = FrameOf(U16(0x34), 0);
				DrawSprite(I32(0x0c) + 0x100000, (0x20 - I32(0x54)) * 0x10000 + I32(0x10), 3, 0, 0, 0, U32(0x28));
			}
			if (p->GetLatchedPose() != 0x3b) {
				U16(0x34) = Anim(AnimSet::Zuiger, 1);
				U32(0x28) = FrameOf(U16(0x34), 0);
				DrawSprite(I32(0x0c) + 0x100000, I32(0x10) - 0x50000, 3, 0, 0, 0, U32(0x28));
			}
		}
		std::uint32_t trig = _world->Trigger(I32(0x50));
		if (trig != 0) {
			std::int32_t freq;
			if (trig < 0x20) {
				freq = (std::int32_t)(trig * 0x5622) >> 5;
			} else if (trig < 0xe0) {
				freq = 0x5622;
			} else {
				freq = DivPow2((0xff - (std::int32_t)trig) * 0x5622, 5);
			}
			HoldSample(I32(0x0c), I32(0x10), 0x42, 100, freq);
		}
		std::int32_t pX = p->X16(), pY = p->Y16();
		if (I32(0x0c) + 0x150000 < pX && pX < I32(0x0c) + 0x2b0000) {
			std::int32_t y = I32(0x10);
			if (y + I32(0x54) * -0x10000 < pY && pY < y + 0x400000 && _world->Trigger(I32(0x50)) != 0 && y - 0x200000 < pY &&
				p->TransporterTicks() != _slot) {
				p->SetPoseFromObject(0x3b, 0);
				p->TransporterTicks() = _slot;
				p->ExtVY() = 0;
				p->ExtVX() = 0;
				p->VX16() = 0;
				p->VY16() = 0;
				PlaySample(0, 0, 0x1b, 0, 0);
				LOGI("[Zuiger] tick {} slot {} captures the player", p->GetTickCount(), _slot);
			}
		}
	}

	// ---- 0x32 drip, 0x36 rain --------------------------------------------------------------------------------

	void DripEmitter::Behave()
	{
		// LAB_004059f0: a drop every p(0,8) seconds (at least one), from a phase of p(8,8) tenths
		if (U8(0x41) == 0) {
			std::int32_t tx = CellX(), ty = CellY();
			std::int32_t interval = _world->GetEventParam(tx, ty, 0, 8) * 0x46;
			I32(0x50) = (interval < 0x46 ? 0x46 : interval);
			U8(0x41) = 0x1c;
			I32(0x24) = _world->GetEventParam(tx, ty, 8, 8) * 7;
		}
		if (I32(0x50) <= ++I32(0x24)) {
			I32(0x24) = 0;
			_world->SpawnDrip(I32(0x0c), I32(0x10));
		}
	}

	void RainGenerator::Behave()
	{
		// LAB_00405890: one drop per tick over the view ±128 px, from 32 px above it, where the sky is open
		BatterymanPlayer* p = _world->GetPlayer();
		if (U8(0x41) == 0) {
			BatteryWorld::DropDamage() = _world->GetEventParam(CellX(), CellY(), 0, 5) << 4;
			U8(0x41) = 8;
			LOGI("[Rain] slot {}: drop damage {}", _slot, BatteryWorld::DropDamage());
		}
		std::uint32_t r = _world->Random();
		std::int32_t x = p->GetCameraX16() - 0x800000 + (std::int32_t)(r & 0x7fff) * (BatterymanPlayer::ViewWidth + 0x100) * 2;
		std::int32_t tx = DivPow2(x, 21);
		std::int32_t rows = _world->GetEventGridSize().Y;
		std::int32_t row = 0;
		while (!_world->HasMainLayerTile(tx, row)) {
			row++;
			if (rows <= row) {
				break;
			}
		}
		std::int32_t top = p->GetCameraY16() - 0x200000;
		if (top < row * 0x200000) {
			std::uint32_t r2 = _world->Random();
			_world->SpawnRainDrop(x, (std::int32_t)(r2 & 0x7fd7) * 0x40 + top);
		}
	}

	// ---- 0x3b, 0x41, 0x46, 0x37 ------------------------------------------------------------------------------

	void InertMarker::Behave()
	{
		// LAB_00404fc0
		_world->Destroy(_slot);
	}

	void WaterColour::Behave()
	{
		// LAB_00404f50
		std::int32_t tx = CellX(), ty = CellY();
		BatteryWorld::WaterSurfaceRemap() = (std::uint8_t)_world->GetEventParam(tx, ty, 0, 8);
		BatteryWorld::WaterBodyRemap() = (std::uint8_t)_world->GetEventParam(tx, ty, 8, 8);
		LOGI("[WaterColour] remap rows: surface 0x{:.2x}, body 0x{:.2x}", BatteryWorld::WaterSurfaceRemap(), BatteryWorld::WaterBodyRemap());
		_world->Destroy(_slot);
	}

	void SparkEmitter::Behave()
	{
		// LAB_00401e10: 1 tick in 256, 1..8 sparks if the player is within 400 × 300 px
		BatterymanPlayer* p = _world->GetPlayer();
		std::uint32_t r = _world->Random();
		if ((std::int8_t)r == (std::int8_t)0x8c) {
			std::int32_t pX = p->X16(), pY = p->Y16();
			if (I32(0x0c) - 0x1900000 < pX && pX < I32(0x0c) + 0x1900000 && I32(0x10) - 0x12c0000 < pY && pY < I32(0x10) + 0x12c0000) {
				std::int32_t n = (std::int32_t)(_world->Random() & 7) + 1;
				do {
					_world->SpawnSpark(I32(0x0c), I32(0x10), 1);
				} while (0 < --n);
			}
		}
	}

	void BossEye::Behave()
	{
		// LAB_00403e40
		BatterymanPlayer* p = _world->GetPlayer();
		if (U8(0x41) == 0) {
			std::int32_t tx = CellX(), ty = CellY();
			I32(0x50) = _world->GetEventParam(tx, ty, 0, 1);
			U16(0x34) = Anim(AnimSet::BaasOgen, I32(0x50));
			U32(0x28) = FrameOf(U16(0x34), 0);
			S8(0x45) = (std::int8_t)(_world->GetEventParam(tx, ty, 1, 1) * 2 - 1);
			U8(0x41) = 0x1c;
		}
		std::int32_t k = ++I32(0x24);
		if (_world->IsDrawingTick()) {
			std::int32_t fc = FrameCountOf(U16(0x34));
			std::int8_t f = (std::int8_t)(fc > 0 ? DivPow2(k, 2) % fc : 0);
			U8(0x3c) = (std::uint8_t)f;
			U32(0x28) = FrameOf(U16(0x34), f);
			DrawSprite(I32(0x0c), I32(0x10), 3, S8(0x45), 0, 0, U32(0x28));
			std::int32_t dx = std::clamp(DivPow2(p->X16() - I32(0x0c), 5), -0x30000, 0x30000);
			std::int32_t dy = std::clamp(DivPow2(p->Y16() - I32(0x10), 5), -0x20000, 0x20000);
			DrawSprite(dx + I32(0x0c), dy + I32(0x10), 3, S8(0x45), 0, 0, FrameOf(Anim(AnimSet::BaasOgen, 2), 0));
		}
	}

	// ---- 0x3a PARAPLU ----------------------------------------------------------------------------------------

	void Umbrella::Behave()
	{
		// LAB_00402db0
		BatterymanPlayer* p = _world->GetPlayer();
		std::uint32_t t = p->GetTickCount();
		if (U8(0x41) == 0) {
			std::int32_t tx = CellX(), ty = CellY();
			I32(0x50) = _world->GetLinkId(tx, ty);
			I32(0x54) = _world->GetEventParam(tx, ty, 6, 3);
			I32(0x58) = _world->GetEventParam(tx, ty, 9, 1) * 2 - 1;
			I32(0x5c) = _world->GetEventParam(tx, ty, 10, 4) << 16;
			I32(0x54) += _world->GetEventParam(tx, ty, 14, 3) * 8;
			I32(0x6c) = I32(0x10);
			I32(0x18) = 0;
			I32(0x60) = _world->ActivationCount(I32(0x50));
			I32(0x14) = 0;
			U8(0x41) = 0x1c;
			I32(0x6c) = I32(0x10);
			I32(0x70) = 0;
		}
		if ((std::uint32_t)_world->ActivationCount(I32(0x50)) != (std::uint32_t)I32(0x60)) {
			U8(0x41) = 8;
			I32(0x60) = _world->ActivationCount(I32(0x50));
			I32(0x1c) = I32(0x58) << 11;
			I32(0x14) = 0;
			I32(0x64) = 0;
		}
		if (U8(0x41) == 8) {
			HoldSample(I32(0x0c), I32(0x10), 0x38, 0x80, (Abs(I32(0x14)) >> 6) + 8000);
			if (Sign(I32(0x14)) == Sign(I32(0x1c))) {
				I32(0x70) = 0;
				I32(0x14) += I32(0x1c);
			} else {
				I32(0x14) += I32(0x1c) * 4;
			}
			std::int32_t m = I32(0x5c);
			if (m < I32(0x14) || (m = -m, I32(0x14) < m)) {
				I32(0x14) = m;
			}
			I32(0x0c) += I32(0x14);
			if (I32(0x70) == 0) {
				std::int32_t x = (-1 < I32(0x14) ? I32(0x0c) + 0x500000 : I32(0x0c) - 0x500000);
				std::int32_t y = I32(0x08);
				if (_world->GetEventType(TileOf(x), TileOf(y)) == 0x27 || _world->TestPixel(x, y) != 0) {
					std::int32_t a = Abs(I32(0x1c));
					I32(0x1c) = (I32(0x14) < 1 ? a : -a);
					I32(0x70) = 1;
					if (I32(0x54) <= ++I32(0x64)) {
						U8(0x41) = 0x1c;
						I32(0x64) = 0;
					}
				}
			}
		} else {
			if (I32(0x70) == 0 || Sign(I32(0x14)) == Sign(I32(0x1c))) {
				I32(0x70) = 0;
				I32(0x14) = 0;
			} else {
				I32(0x14) += I32(0x1c) * 0x10;
			}
			I32(0x0c) += I32(0x14);
		}
		I32(0x10) = I32(0x6c) + BatteryWorld::Sine((std::int32_t)((t * 8) & 0x3ff)) * 8;
		U16(0x34) = Anim(AnimSet::Paraplu, 0);
		U32(0x28) = FrameOf(U16(0x34), (std::int32_t)((t >> 2) & 3));
		DrawSprite(I32(0x0c), I32(0x10), 3, 0, 9, 0, U32(0x28));
	}

	// ---- 0x33 SUCKER / 0x34 BIGSUCKER ------------------------------------------------------------------------

	void Sucker::Behave()
	{
		// LAB_00401f50 / LAB_00404250 - the same machine; the big one walks with two gaits and bites harder
		BatterymanPlayer* p = _world->GetPlayer();
		AnimSet set = (_big ? AnimSet::BigSucker : AnimSet::Sucker);
		std::int32_t walkAnim = (_big ? 7 : 6);
		std::int32_t lungeAnim = (_big ? 3 : 6);
		auto anim = [&](std::int32_t k) { return Anim(set, k); };
		auto frameCount = [&]() { return FrameCountOf(U16(0x34)); };
		auto frameNow = [&]() { return FrameOf(U16(0x34), S8(0x3c)); };
		auto drawFacingAway = [&]() {
			U32(0x28) = frameNow();
			DrawSprite(I32(0x0c), I32(0x10), 3, -S8(0x45), 0, 0, U32(0x28));
		};
		auto startDrowning = [&]() {
			U8(0x41) = 3;
			U16(0x34) = anim(5);
			U8(0x3c) = 0;
			I32(0x24) = 0;
			U32(0x28) = FrameOf(U16(0x34), 0);
		};
		auto land = [&]() {
			U16(0x34) = anim(walkAnim);
			_world->SnapToGround(this);
			U8(0x41) = 5;
			I32(0x18) = 0;
			U8(0x3c) = 0;
			I32(0x14) = 0x100000;
		};
		auto fall = [&]() {
			std::int32_t vy = I32(0x18) + 0x4000;
			I32(0x18) = vy;
			I32(0x10) += vy;
			if (0x60000 < vy) {
				I32(0x18) = 0x60000;
			}
		};

		if (U8(0x41) != _tracedState) {
			LOGI("[Sucker] tick {} slot {} state 0x{:.2x} -> 0x{:.2x} at ({}, {}), player ({}, {})", p->GetTickCount(), _slot,
				_tracedState, U8(0x41), I32(0x0c) >> 16, I32(0x10) >> 16, p->X16() >> 16, p->Y16() >> 16);
			_tracedState = U8(0x41);
		}

		switch (U8(0x41)) {
			case 0: {
				if (_world->GetEventParam(CellX(), CellY(), 0, 1) != 0) {
					// Hangs from the ceiling
					U8(0x41) = 0x24;
					I32(0x24) = 0;
					U16(0x34) = anim(1);
					I32(0x10) += 0x80000;
					return;
				}
				U8(0x3c) = 0;
				U16(0x34) = anim(walkAnim);
				U32(0x28) = FrameOf(U16(0x34), 0);
				_world->SnapToGround(this);
				U8(0x41) = 5;
				I32(0x18) = 0;
				S8(0x45) = 1;
				return;
			}
			case 3: {
				// Drowning: floats at the surface, then dissolves into pixels
				std::int32_t k = I32(0x24) + 1;
				BatteryObject* water = _world->GetObject(I32(0x6c));
				I32(0x10) = (water != nullptr ? water->I32(0x10) : 0);
				I32(0x24) = k;
				if (4 < k) {
					I32(0x24) = 0;
					if (++S8(0x3c) == 8) {
						PlaySample(I32(0x0c), I32(0x10), 0x16, 0x20, 0);
					}
				}
				if (frameCount() <= S8(0x3c)) {
					S8(0x3c) = (std::int8_t)((std::uint8_t)frameCount() - 1);
					U32(0x28) = frameNow();
					_world->Shatter(this, 2);
					LOGI("[Sucker] tick {} slot {} drowned", p->GetTickCount(), _slot);
					_world->Destroy(_slot);
					return;
				}
				drawFacingAway();
				return;
			}
			case 0x1b:
				if (!_big) {
					return;
				}
				[[fallthrough]];
			case 5: {
				// Walking: a step every 7 ticks (the big one's chase: 14 px every 4)
				std::int32_t interval;
				if (!_big) {
					HoldSample(I32(0x0c), I32(0x10), 0x3f, 0x40, 0);
					U16(0x34) = anim(6);
					I32(0x14) = 0x100000;
					interval = 6;
				} else if (U8(0x41) == 5) {
					I32(0x14) = 0x100000;
					interval = 6;
					U16(0x34) = anim(7);
				} else {
					U16(0x34) = anim(6);
					I32(0x14) = 0xe0000;
					interval = 3;
					HoldSample(I32(0x0c), I32(0x10), 9, 0x80, 0);
				}
				std::int32_t k = ++I32(0x24);
				if (interval < k) {
					S8(0x3c)++;
					I32(0x24) = 0;
					if (!_big) {
						I32(0x0c) += S8(0x45) * 0x100000;
						I32(0x6c) = _world->FindWaterAt(I32(0x0c), I32(0x10));
						if (I32(0x6c) != 0) {
							startDrowning();
						}
					} else {
						I32(0x0c) += (std::int32_t)S8(0x45) * I32(0x14);
					}
				}
				// Turns at a wall 48 px ahead or where the ground ends 32 px ahead (at the frame's coldspot height)
				std::uint32_t ahead = _world->TestPixel(S8(0x45) * 0x300000 + I32(0x0c), I32(0x10));
				const FrameInfo* f = BatteryAnims::Get().GetFrame(U32(0x28));
				std::int32_t footY = (f != nullptr ? f->HotspotY - f->ColdspotY : 0);
				std::uint32_t ground = _world->TestPixel(S8(0x45) * 0x200000 + I32(0x0c), footY * 0x10000 + I32(0x10));
				if (ground == 0 || ahead != 0) {
					S8(0x45) = -S8(0x45);
				}

				std::int32_t pX = p->X16(), pY = p->Y16();
				if (p->Energy() < 1) {
					U8(0x41) = 5;
				} else if (I32(0x10) - 0x400000 < pY && pY < I32(0x10) + 0x400000) {
					std::int8_t facing = S8(0x45);
					std::int32_t x = I32(0x0c);
					if (_big) {
						bool chase = ((0 < facing && x < pX && pX < x + 0x1900000) || (facing < 0 && pX < x && x - 0x1900000 < pX));
						U8(0x41) = (chase ? 0x1b : 5);
					} else {
						// (A drowning sucker that sees the player walks on: the state is set before the test)
						U8(0x41) = 5;
					}
					if (p->TransporterTicks() == 0 && p->HazardSlot() == 0 &&
						((0 < facing && x < pX && pX < x + 0xa00000) || (facing < 0 && pX < x && x - 0xa00000 < pX))) {
						// Lunges at the player in front within 160 px
						U8(0x41) = 6;
						I32(0x18) = -0x60000;
						U8(0x3c) = 0;
						U16(0x34) = anim(lungeAnim);
						std::int32_t vx = (pX - x) / (std::int32_t)((std::uint32_t)frameCount() << 2);
						I32(0x14) = std::clamp(vx, -0x60000, 0x60000);
						LOGI("[Sucker] tick {} slot {} lunges, vx {:.3f}", p->GetTickCount(), _slot, I32(0x14) / 65536.0f);
					}
				}
				if (frameCount() <= S8(0x3c)) {
					U8(0x3c) = 0;
				}
				drawFacingAway();
				return;
			}
			case 6: {
				// The lunge: the bite lands at the end of the animation, if the player is still close
				if (!_big) {
					HoldSample(I32(0x0c), I32(0x10), 0x3f, 0x40, 0);
				} else {
					HoldSample(I32(0x0c), I32(0x10), 9, 0x80, 0);
				}
				if (4 < ++I32(0x24)) {
					I32(0x24) = 0;
					S8(0x3c)++;
				}
				if (_world->TestPixel(S8(0x45) * (_big ? 0x300000 : 0x100000) + I32(0x0c), I32(0x10)) != 0) {
					I32(0x14) = 0;
				}
				std::int32_t x = I32(0x0c) + I32(0x14);
				std::int32_t y = I32(0x10) + I32(0x18);
				I32(0x18) += 0x4000;
				I32(0x0c) = x;
				I32(0x10) = y;
				I32(0x6c) = _world->FindWaterAt(x, y);
				if (I32(0x6c) == 0) {
					if (S8(0x3c) == frameCount() - 2 &&
						(!_world->IsPlayerWithin(this, 0x1000) || p->HazardSlot() != 0 || p->TransporterTicks() != 0)) {
						U8(0x41) = 0x14;
					}
				} else {
					startDrowning();
				}
				if (frameCount() <= S8(0x3c)) {
					U8(0x3c) = 0;
					U8(0x41) = 0x1d;
					p->Energy() -= (_big ? 0x578 : 700);
					p->HazardSlot() = _slot;
					p->VX16() = 0;
					p->Ax16() = 0;
					p->VY16() = 0;
					p->TransporterTicks() = 0;
					p->SetPoseFromObject(_big ? 0x18 : 0x17, 0);
					U32(0x28) = 0;
					LOGI("[Sucker] tick {} slot {} bites: energy {}", p->GetTickCount(), _slot, p->Energy());
					return;
				}
				drawFacingAway();
				return;
			}
			case 0x13: {
				// Dropping from the ceiling
				std::int32_t k = ++I32(0x24);
				if (I32(0x50) == 0) {
					if (4 < k) {
						S8(0x3c)++;
						I32(0x24) = 0;
						if (frameCount() <= S8(0x3c)) {
							U8(0x3c) = 0;
							U16(0x34) = anim(0);
							I32(0x50) = 1;
							I32(0x18) = 0;
						}
					}
				} else {
					fall();
					if (_world->TestPixel(I32(0x0c), I32(0x10) + 0x140000) != 0) {
						land();
						S8(0x45) = (p->X16() < I32(0x0c) ? -1 : 1);
					}
				}
				U32(0x28) = frameNow();
				DrawSelf();
				return;
			}
			case 0x14: {
				// Missed: falls back to the ground
				if (_world->TestPixel(S8(0x45) * 0x300000 + I32(0x0c), I32(0x10)) != 0) {
					I32(0x14) = 0;
				}
				std::int32_t vy = I32(0x18) + 0x4000;
				I32(0x0c) += I32(0x14);
				I32(0x10) += I32(0x18);
				I32(0x18) = vy;
				if (0x60000 < vy) {
					I32(0x18) = 0x60000;
				}
				I32(0x6c) = _world->FindWaterAt(I32(0x0c), I32(0x10));
				if (I32(0x6c) == 0) {
					if (_world->TestPixel(S8(0x45) * 0x140000 + I32(0x0c), I32(0x10) + 0x140000) != 0) {
						land();
						S8(0x45) = (I32(0x0c) <= p->X16() ? 1 : -1);
					}
				} else {
					startDrowning();
				}
				drawFacingAway();
				return;
			}
			case 0x1c: {
				// Chewing
				if (4 < ++I32(0x24)) {
					I32(0x24) = 0;
					std::int8_t f = ++S8(0x3c);
					if (f == 1) {
						PlaySample(I32(0x0c), I32(0x10), (_big ? 10 : 0x40), 0, 0);
					} else if (f == 10) {
						PlaySample(I32(0x0c), I32(0x10), (_big ? 0xb : 0x41), 0, 0);
					}
				}
				if (frameCount() <= S8(0x3c)) {
					if (++I32(0x50) < 0x24) {
						S8(0x3c) = (std::int8_t)((std::uint8_t)frameCount() - 1);
					} else {
						U8(0x3c) = 0;
						U8(0x41) = 5;
						I32(0x24) = 0;
						U16(0x34) = anim(walkAnim);
					}
				}
				drawFacingAway();
				return;
			}
			case 0x1d: {
				// Holding the player (not drawn) until the controller lets go
				if (p->HazardSlot() == 0) {
					I32(0x18) = 0;
					I32(0x14) = 0;
					U8(0x41) = 0x20;
					U16(0x34) = anim(0);
					U8(0x3c) = 0;
					I32(0x24) = 0;
					I32(0x50) = 0;
					U32(0x28) = FrameOf(U16(0x34), 0);
				}
				return;
			}
			case 0x20: {
				// Spits the player out, falls, and chews
				std::int32_t k = ++I32(0x24);
				if (I32(0x50) == 0) {
					if (4 < k) {
						S8(0x3c)++;
						I32(0x24) = 0;
						if (frameCount() <= S8(0x3c)) {
							U8(0x3c) = 0;
							U16(0x34) = anim(0);
							I32(0x50) = 1;
							I32(0x18) = 0;
						}
					}
				} else {
					fall();
					if (_world->TestPixel(I32(0x0c), I32(0x10) + 0x140000) != 0) {
						U16(0x34) = anim(5);
						_world->SnapToGround(this);
						U8(0x41) = 0x1c;
						I32(0x18) = 0;
						U8(0x3c) = 0;
					}
				}
				U32(0x28) = frameNow();
				DrawSelf();
				return;
			}
			case 0x24: {
				// Hanging, until the player comes within 256 px
				if (7 < ++I32(0x24)) {
					S8(0x3c)++;
					I32(0x24) = 0;
					if (frameCount() <= S8(0x3c)) {
						U8(0x3c) = 0;
					}
				}
				U32(0x28) = frameNow();
				DrawSelf();
				if (0 < p->Energy() && _world->IsPlayerWithin(this, 0x10000)) {
					U8(0x41) = 0x13;
					I32(0x50) = 0;
					U16(0x34) = anim(2);
					I32(0x24) = 0;
				}
				return;
			}
			default:
				return;
		}
	}
}
