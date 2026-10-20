#include "BatteryObject.h"
#include "BatteryWorld.h"
#include "BatterymanPlayer.h"
#include "../../ILevelHandler.h"
#include "../../ContentResolver.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/Graphics/RenderQueue.h"

#include <algorithm>

namespace Jazz2::Actors::Batteryman
{
	namespace
	{
		/** @brief Metadata state of an animation in `Interactive/BatteryObjects.res`: 0x10000 + set * 256 + anim */
		constexpr std::uint32_t AnimStateOf(std::uint32_t set, std::uint32_t anim) {
			return 0x10000u + (set << 8) + anim;
		}
	}

	BatteryObject::BatteryObject()
		: _world(nullptr), _slot(0), _spriteCount(0), _heldSoundId(-1), _heldSoundRequested(false)
	{
		std::memset(_record, 0, sizeof(_record));
	}

	BatteryObject::~BatteryObject()
	{
		if (_heldSound != nullptr) {
			_heldSound->stop();
		}
		// Destroyed by the engine (level end) before the pool let go of it
		if (_world != nullptr && _world->GetObject(_slot) == this) {
			_world->Release(_slot);
		}
	}

	Task<bool> BatteryObject::OnActivatedAsync(const ActorActivationDetails& details)
	{
		// The object layer moves and tests everything itself, nothing of this engine's physics applies
		SetState(ActorState::ForceDisableCollisions | ActorState::PreserveOnRollback, true);
		SetState(ActorState::ApplyGravitation | ActorState::CollideWithTileset | ActorState::CollideWithOtherActors |
			ActorState::CollideWithSolidObjects | ActorState::IsSolidObject, false);

		async_await RequestMetadataAsync("Interactive/BatteryObjects"_s);

		// The renderer stays enabled: its visit calls OnDraw(), which draws the queued sprites instead of its own
		async_return true;
	}

	void BatteryObject::OnUpdate(float timeMult)
	{
		// Ticked by BatteryWorld at 70 Hz; only the position is passed on, for culling and positional sound
		_pos = Vector2f((float)(I32(0x0c) >> 16), (float)(I32(0x10) >> 16));
	}

	bool BatteryObject::OnTileDeactivated()
	{
		// The original never culls objects by distance
		return false;
	}

	void BatteryObject::BeginSprites()
	{
		_spriteCount = 0;
	}

	void BatteryObject::DrawSprite(std::int32_t x, std::int32_t y, std::int32_t layer, std::int32_t direction, std::int32_t p5,
		std::int32_t p6, std::uint32_t frameHandle)
	{
		// FUN_0040e080: `if ((DAT_004d8388 == 0) && (0 < param_7))`
		if (_world == nullptr || !_world->IsDrawingTick() || (std::int32_t)frameHandle <= 0) {
			return;
		}
		if (_spriteCount >= (std::int32_t)_sprites.size()) {
			auto& command = _sprites.emplace_back().Command;
			command = std::make_unique<RenderCommand>(RenderCommand::Type::Sprite);
			command->GetMaterial().SetShaderProgramType(Material::ShaderProgramType::Sprite);
			command->GetMaterial().SetBlendingEnabled(true);
			command->GetMaterial().ReserveUniformsDataMemory();
			command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);
			auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
			if (textureUniform != nullptr && textureUniform->GetIntValue(0) != 0) {
				textureUniform->SetIntValue(0); // GL_TEXTURE0
			}
		}
		SpriteRequest& r = _sprites[_spriteCount++];
		r.X = x; r.Y = y; r.Layer = layer; r.Direction = direction; r.P5 = p5; r.P6 = p6; r.Frame = frameHandle;
		// Where the original's flush puts it: after tile layer `layer`, in its pass, in enqueue order
		r.RenderLayer = _world->GetSpriteRenderLayer(layer, p5 & 0x1f);
		r.VisitOrder = _world->NextSpriteSequence();
	}

	void BatteryObject::DrawSelf()
	{
		// FUN_0040dec0 (its own view culling is the renderer's here)
		std::uint32_t frame = U32(0x28);
		const FrameInfo* f = BatteryAnims::Get().GetFrame(frame);
		std::int32_t mode = (U8(0x46) != 0 ? 5 : (f != nullptr && f->Translucent ? 1 : 0));
		DrawSprite(I32(0x0c), I32(0x10), 3, S8(0x45), mode, (U8(0x46) != 0 ? 0xf : 0), frame);
	}

	void BatteryObject::Touch(std::int32_t arg)
	{
		// FUN_0040bd00(slot, player) - by the object's class
		if (_world == nullptr) {
			return;
		}
		BatterymanPlayer* p = _world->GetPlayer();
		switch (GetClass()) {
			case 2: {
				// Boss projectile: -1400 energy, 35 ticks of hurt lock, sBAAS_D002C, the projectile bursts (state 10)
				p->Energy() += -0x578;
				p->HurtTimer() = 0x23;
				PlaySample(0, 0, 3, 0, 0);
				U8(0x41) = 10;
				p->SetPoseFromObject(0x16, 1);
				break;
			}
			case 5: {
				// Pickups: the reward once (class 6 afterwards matches nothing), the sound at the pickup. 0x45 has no case
				std::int32_t sample = 0;
				switch (U8(0x44)) {
					case 0x3c: sample = 0x2e; p->Lives() += 1; U8(0x41) = 0x1f; U8(0x40) = 6; break;
					case 0x3e: sample = 0x2c; p->Batteries() += 1; U8(0x41) = 0x1f; U8(0x40) = 6; break;
					case 0x3f: sample = 0x2d; p->Superbatteries() += 1; U8(0x41) = 0x1f; U8(0x40) = 6; break;
				}
				if (sample != 0) {
					PlaySample(I32(0x0c), I32(0x10), sample, 0, 0);
					LOGI("[BatteryObject] tick {} slot {} pickup 0x{:.2x} collected: lives {} batteries {} superbatteries {}",
						p->GetTickCount(), _slot, U8(0x44), p->Lives(), p->Batteries(), p->Superbatteries());
				}
				break;
			}
			default:
				// Class 0 (an empty stub), 8 (most objects) and 6 (collected): nothing
				break;
		}
	}

	std::shared_ptr<AudioBufferPlayer> BatteryObject::StartSample(std::int32_t x, std::int32_t y, std::int32_t id,
		std::int32_t volume, std::int32_t freq, bool looping)
	{
		if (_world == nullptr) {
			return nullptr;
		}
		std::int32_t v, pan;
		_world->ComputeMix(x, y, volume, v, pan);
		if (v < 4) {
			return nullptr;
		}
		// Every sample but one is 22050 Hz; a given playback rate is relative to that
		float pitch = (freq == 0 ? 1.0f : (float)freq / 22050.0f);
		auto player = PlaySfx(BatteryWorld::GetSampleName(id), (float)v / 63.0f, pitch, looping ? SfxFlags::Looping : SfxFlags::None);
		if (player != nullptr) {
			// The original's fixed pan, not this engine's distance model (the volume above already has the distance)
			player->setAs2D(true);
			player->setPosition(Vector3f((float)(pan - 0x20) / 32.0f, 0.0f, 0.0f));
		}
		return player;
	}

	void BatteryObject::PlaySample(std::int32_t x, std::int32_t y, std::int32_t id, std::int32_t volume, std::int32_t freq)
	{
		StartSample(x, y, id, volume, freq, false);
	}

	void BatteryObject::HoldSample(std::int32_t x, std::int32_t y, std::int32_t id, std::int32_t volume, std::int32_t freq)
	{
		if (_heldSound != nullptr && _heldSoundId == id && !_heldSound->isStopped()) {
			// Channel state 7: kept; position, volume and playback rate follow the call (FUN_00418f90)
			std::int32_t v, pan;
			_world->ComputeMix(x, y, volume, v, pan);
			_heldSound->setGain((float)v / 63.0f * PreferencesCache::MasterVolume * PreferencesCache::SfxVolume);
			_heldSound->setPosition(Vector3f((float)(pan - 0x20) / 32.0f, 0.0f, 0.0f));
			_heldSound->setPitch(freq == 0 ? 1.0f : (float)freq / 22050.0f);
		} else {
			if (_heldSound != nullptr) {
				_heldSound->stop();
			}
			_heldSound = StartSample(x, y, id, volume, freq, true);
			_heldSoundId = id;
		}
		_heldSoundRequested = true;
	}

	void BatteryObject::ReleaseHeldSample()
	{
		if (!_heldSoundRequested && _heldSound != nullptr) {
			_heldSound->stop();
			_heldSound = nullptr;
		}
		_heldSoundRequested = false;
	}

	bool BatteryObject::OnDraw(RenderQueue& renderQueue)
	{
		if (_metadata == nullptr) {
			return true;
		}

		auto& resolver = ContentResolver::Get();
		for (std::int32_t i = 0; i < _spriteCount; i++) {
			SpriteRequest& r = _sprites[i];
			std::uint32_t v = r.Frame - 1;
			std::uint32_t set = (v >> 16), anim = ((v >> 8) & 0xff), frame = (v & 0xff);
			auto* res = _metadata->FindAnimation((AnimState)AnimStateOf(set, anim));
			if (res == nullptr || res->Base == nullptr || res->Base->TextureDiffuse == nullptr || (std::int32_t)frame >= res->FrameCount) {
				continue;
			}

			auto* base = res->Base;
			auto* command = r.Command.get();
			bool indexed = ((base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed);
			resolver.ConfigureSpriteShader(*command, indexed);

			Vector2i texSize = base->TextureDiffuse->GetSize();
			Recti frameRect = base->GetFrameRect(res->FrameOffset + (std::int32_t)frame);
			float texScaleX = (float(frameRect.W) / float(texSize.X));
			float texBiasX = (float(frameRect.X) / float(texSize.X));
			float texScaleY = (float(frameRect.H) / float(texSize.Y));
			float texBiasY = (float(frameRect.Y) / float(texSize.Y));

			// The original puts the frame's top-left at (x + hotX, y + hotY), mirrored about x when the direction is
			// negative. The converter puts the frame at (origin + hot) in the animation's cell, origin being the
			// common hotspot plus the 2 px border; a tightly packed frame keeps only its own area and records where
			// that begins in the cell, so the quad moves by that offset, and a mirrored frame mirrors within that
			// area (teardown/90 §2.1)
			Vector2i origin = FrameOrigin(*base);
			Vector2i offset = base->GetFrameOffset(res->FrameOffset + (std::int32_t)frame);
			std::int32_t px = (r.X + ((r.X >> 31) & 0xffff)) >> 16;
			std::int32_t py = (r.Y + ((r.Y >> 31) & 0xffff)) >> 16;
			float left;
			if (r.Direction < 0) {
				texBiasX += texScaleX;
				texScaleX = -texScaleX;
				left = (float)(px + origin.X - offset.X - frameRect.W);
			} else {
				left = (float)(px - origin.X + offset.X);
			}
			float top = (float)(py - origin.Y + offset.Y);
			std::int32_t height = frameRect.H;

			if ((r.P5 & 0x1f) == 8) {
				// Mode 8, clip-top: only the rows below the frame's top + param are drawn
				const FrameInfo* info = BatteryAnims::Get().GetFrame(r.Frame);
				if (info != nullptr) {
					std::int32_t cut = (py + info->HotspotY + (std::uint8_t)r.P6) - (std::int32_t)top;
					cut = std::clamp(cut, 0, height);
					top += (float)cut;
					texBiasY += (float)cut / float(texSize.Y);
					texScaleY = (float)(height - cut) / float(texSize.Y);
					height -= cut;
				}
				if (height <= 0) {
					continue;
				}
			}

			auto* instanceBlock = command->GetInstanceBlock();
			instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
			instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue((float)frameRect.W, (float)height);
			// Mode 1, a translucent frame: blend[src][dst], the nearest colour to (2·src + dst) / 3 for the sprite's
			// colours (FUN_00411180) - drawn at 2/3 opacity
			Colorf color = ((r.P5 & 0x1f) == 1 ? Colorf(1.0f, 1.0f, 1.0f, 2.0f / 3.0f) : Colorf::White);
			instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(color.Data());
			command->SetTransformation(Matrix4x4f::Translation(left, top, 0.0f));
			command->SetLayer(r.RenderLayer);
			command->SetVisitOrder(r.VisitOrder);
			resolver.BindSpritePalette(*command, *base->TextureDiffuse.get(), indexed, res->PaletteOffset);
			renderQueue.AddCommand(command);
		}
		return true;
	}
}
