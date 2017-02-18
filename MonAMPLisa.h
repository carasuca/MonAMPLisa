#pragma once

#include <amp.h>
#include <amp_graphics.h>
#include <numeric> // accumulate

template<int sel, int pop> // selection vs population count
struct MonAMPLisa
{
	typedef unsigned char Byte;
	MonAMPLisa(const Byte src_[], int w, int h)
		: scores(pop)
		, src(h, w, src_, w*h, 8U)
		, best(sel, h, w, 8U)
		, best2(sel, h, w, 8U)
		, boxes(pop)
	{}

	unsigned Step(unsigned step)
	{
		MutateBoxes(step);
		CrossMutateScore();
		UpdateBest();
		SwapBest();
		const auto ptr = scores.data();
		return std::accumulate(ptr, ptr + sel, 0) / sel;
	}
	void GetSource(Byte data[], size_t bytes) const
	{
		concurrency::graphics::copy(src, data, bytes);
	}
	void GetPreview(Byte data[], size_t bytes, int id = 0) const
	{
		assert(id < sel);

		concurrency::graphics::copy(GetCurrentBest(),
			concurrency::index<3>(id, 0, 0),
			concurrency::extent<3>(1, best.extent[1], best.extent[2]),
			data, bytes);
	}

private:

	struct Box // aka mutation
	{
		unsigned val;
		concurrency::index<2> pos;
		concurrency::extent<2> ext;

		unsigned mix(unsigned val1, unsigned val2, concurrency::index<2> uv) const restrict(amp)
		{
			bool contained = contains(uv);
			unsigned val3 = contained*val;
			return (val1 + val2 + val3) / (2 + contained);
		}

		unsigned mix(unsigned val1, unsigned val2, int j, int i) const restrict(amp)
		{
			return mix(val1, val2, concurrency::index<2>(j, i));
		}

		bool contains(concurrency::index<2> uv) const restrict(amp) {
			return ext.contains(uv - pos);
		}

		void update(int h, int w, unsigned r1, unsigned r2, unsigned r3)
		{
			val = r1 >> 24;
			int w1 = (r2 >> 16) % w;
			int w2 = r2 % w;
			int h1 = (r3 >> 16) % h;
			int h2 = r3 % h;
			auto ww = std::minmax(w1, w2);
			auto hh = std::minmax(h1, h2);

			pos[0] = hh.first;
			pos[1] = ww.first;
			ext[0] = hh.second - hh.first;
			ext[1] = ww.second - ww.first;
		}

		void update(concurrency::extent<3> idx, unsigned r1, unsigned r2, unsigned r3)
		{
			update(idx[1], idx[2], r1, r2, r3);
		}
	};

	// https://en.wikipedia.org/wiki/Xorshift
	unsigned xorshift32(unsigned x)
	{
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		return x;
	}

	unsigned xrand()
	{
		static unsigned last = 0x87654321;
		return last = xorshift32(last);
	}

	void MutateBoxes(unsigned step)
	{
		for (auto ptr = boxes.data(), ptre = ptr + pop; ptr != ptre; ptr++)
			ptr->update(best.extent, xrand(), xrand(), xrand());
	}

	void CrossMutateScore()
	{
		scores.discard_data();
		const auto& best = GetCurrentBest();
		const auto& scores = this->scores;
		const auto& src = this->src;

		const concurrency::array_view<const Box> boxes = this->boxes;

		static const int TJ = 32, TI = 32;
		auto tile = concurrency::extent<3>(pop, TJ, TI).tile<1, TJ, TI>();

		concurrency::parallel_for_each(tile, [=, &best, &src](concurrency::tiled_index<1, TJ, TI> idx)restrict(amp){
			const int ej = best.extent[1], ei = best.extent[2];
			const int id = idx.global[0], idm = id % sel, idd = (id / sel) % sel;

			tile_static unsigned sum_tile[TJ][TI];

			int lj = idx.local[1], li = idx.local[2];

			unsigned sum_part = 0;
			for (int j = lj; j < ej; j += TJ)
				for (int i = li; i < ei; i += TI)
				{
					const concurrency::index<3>
						mummy(idm, j, i),
						daddy(idd, j, i);
					unsigned res = boxes[id].mix(best[mummy], best[daddy], j, i);
					int d = src(j, i) - res;
					sum_part += d*d;
				}
			sum_tile[lj][li] = sum_part;


			idx.barrier.wait();
			if (lj || li) return;

			unsigned sum = 0;
			for (int j = 0; j != TJ; j++) for (int i = 0; i != TI; i++)
			{
				sum += sum_tile[j][i];
			}

			scores[id].id = id;
			scores[id].score = sum;
		});

	}



	void UpdateBest()
	{
		// sort scores
		auto ptr = scores.data();
		std::partial_sort(ptr, ptr + sel, ptr + pop);

		const concurrency::array_view<const Box> boxes = this->boxes;
		const concurrency::array_view<const ScoredIndex> scores = this->scores;
		// recreate boxes for best scores 
		// -> still got them

		const auto& best = GetCurrentBest();
		auto& next_best = GetNextBest();

		// crossover
		concurrency::parallel_for_each(best.extent, [=, &best, &next_best](concurrency::index<3> idx) restrict(amp){
			const int id = scores[idx[0]].id, j = idx[1], i = idx[2];
			const concurrency::index<3>
				mummy(id % sel, j, i),
				daddy((id / sel) % sel, j, i);

			unsigned res = boxes[id].mix(best[mummy], best[daddy], j, i);
			next_best.set(idx, res);
		});

	}

	typedef concurrency::graphics::texture<unsigned, 2> Texture;
	typedef concurrency::graphics::texture<unsigned, 3> Textures;

	const Textures& GetCurrentBest() const
	{
		return odd_step ? best : best2;
	}

	Textures& GetNextBest()
	{
		return odd_step ? best2 : best;
	}

	void SwapBest() { odd_step ^= true; }

	struct ScoredIndex
	{
		int id;
		unsigned score;

		operator unsigned() const { return score; }
	};

	concurrency::array_view<Box> boxes;
	concurrency::array_view<ScoredIndex> scores;

	const Texture src;

	Textures best, best2;
	bool odd_step = false;

};