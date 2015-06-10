#ifndef SHARD_HPP
#define SHARD_HPP

#include <map>
#include <memory>
#include <sstream>
#include <string>

#define restrict __restrict__
extern "C" {
#include "imhotep_native.h"
#include "table.h"
}

#include "term.hpp"
#include "var_int_view.hpp"

namespace imhotep {

    class Shard {
    public:
        /** A packed_table_ptr should be treated as a simple datum in the
            context of this class and users of it. I.e., these are created
            externally and their lifecycle managed elsewhere. (I'm not aware of
            a C++ pointer idiom appropriate for such a case or I'd use it.) */
        typedef const packed_table_t* packed_table_ptr;

        typedef std::shared_ptr<MMappedVarIntView> var_int_view_ptr;

        explicit Shard(const std::string& dir,
                       packed_table_ptr   table = packed_table_ptr());

        Shard(const Shard& rhs) = default;

        Shard& operator=(const Shard& rhs) = default;

        const std::string& dir() const { return _dir; }

        const packed_table_ptr table() const { return _table; }

        template <typename term_t>
        var_int_view_ptr term_view(const std::string& field) const;

        template <typename term_t>
        var_int_view_ptr docid_view(const std::string& field) const;

        template <typename term_t>
        std::string term_filename(const std::string& field) const;

        template <typename term_t>
        std::string docid_filename(const std::string& field) const;

        std::string split_filename(const std::string& splits_dir,
                                   const std::string& field,
                                   size_t split_num) const;

        std::string name_of() const;

    private:
        std::string base_filename(const std::string& field) const;

        typedef std::map<std::string, var_int_view_ptr> FieldToVarIntView;
        mutable FieldToVarIntView _term_views;
        mutable FieldToVarIntView _docid_views;

        const std::string _dir;
        packed_table_ptr  _table;
    };

} // namespace imhotep

#endif
