#pragma once

#include "file_info.h"

namespace fb2k {
	//! Helper: shared blank file_info object. See: file_info.
	class noInfo_t : public file_info {
		[[noreturn]] static void verboten() { FB2K_BugCheck(); }
	public:
		double		get_length() const override { return 0; }
		replaygain_info	get_replaygain() const override { return replaygain_info_invalid; }

		t_size		meta_get_count() const override { return 0; }
		const char* meta_enum_name(t_size) const override { verboten(); }
		t_size		meta_enum_value_count(t_size) const override { verboten(); }
		const char* meta_enum_value(t_size, t_size) const override { verboten(); }
		t_size		meta_find_ex(const char*, t_size) const override { return SIZE_MAX; }

		t_size		info_get_count() const override { return 0; }
		const char* info_enum_name(t_size) const override { verboten(); }
		const char* info_enum_value(t_size) const override { verboten(); }
		t_size		info_find_ex(const char*, t_size) const override { return SIZE_MAX; }

	private:
		void		set_length(double) override { verboten(); }
		void		set_replaygain(const replaygain_info&) override { verboten(); }

		t_size		info_set_ex(const char*, t_size, const char*, t_size) override { verboten(); }
		void		info_remove_mask(const bit_array&) override { verboten(); }
		t_size		meta_set_ex(const char*, t_size, const char*, t_size) override { verboten(); }
		void		meta_insert_value_ex(t_size, t_size, const char*, t_size) override { verboten(); }
		void		meta_remove_mask(const bit_array&) override { verboten(); }
		void		meta_reorder(const t_size*) override { verboten(); }
		void		meta_remove_values(t_size, const bit_array&) override { verboten(); }
		void		meta_modify_value_ex(t_size, t_size, const char*, t_size) override { verboten(); }

		void		copy(const file_info&) override { verboten(); }
		void		copy_meta(const file_info&) override { verboten(); }
		void		copy_info(const file_info&) override { verboten(); }
		t_size	meta_set_nocheck_ex(const char*, t_size, const char*, t_size) override { verboten(); }
		t_size	info_set_nocheck_ex(const char*, t_size, const char*, t_size) override { verboten(); }

	};

	extern noInfo_t noInfo;
}
