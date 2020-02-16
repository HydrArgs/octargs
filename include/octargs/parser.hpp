#ifndef OCTARGS_PARSER_HPP_
#define OCTARGS_PARSER_HPP_

#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "argument.hpp"
#include "argument_table.hpp"
#include "exception.hpp"
#include "internal/char_utils.hpp"
#include "internal/misc.hpp"
#include "internal/parser_data.hpp"
#include "internal/results_data.hpp"
#include "results.hpp"

namespace oct
{
namespace args
{

template <typename TRAITS, typename VALUES_STORAGE = internal::null_values_storage>
class basic_parser
{
public:
    using values_storage_type = VALUES_STORAGE;

    using argument_type = basic_argument<TRAITS, VALUES_STORAGE>;
    using switch_argument_type = switch_argument<TRAITS, VALUES_STORAGE>;
    using valued_argument_type = valued_argument<TRAITS, VALUES_STORAGE>;
    using positional_argument_type = positional_argument<TRAITS, VALUES_STORAGE>;

    using char_type = typename TRAITS::char_type;
    using string_type = typename TRAITS::string_type;
    using string_vector_type = typename TRAITS::string_vector_type;

    using argument_table_type = basic_argument_table<TRAITS>;
    using results_type = basic_results<TRAITS>;

    basic_parser()
        : m_data_ptr(std::make_shared<parser_data_type>())
    {
        // noop
    }

    DEPRECATED argument_type& add_valarg(const string_vector_type& names)
    {
        return add_valued(names);
    }

    switch_argument_type& add_switch(const string_vector_type& names)
    {
        check_names(names);

        auto new_argument = std::make_shared<switch_argument_type>(names);

        add_to_names_repository(new_argument);

        return *new_argument;
    }

    valued_argument_type& add_valued(const string_vector_type& names)
    {
        check_names(names);

        auto new_argument = std::make_shared<valued_argument_type>(names);

        add_to_names_repository(new_argument);

        return *new_argument;
    }

    argument_type& add_positional(const string_type& name, bool required, bool multivalue)
    {
        auto names = { name };

        check_names(names);

        if (!m_data_ptr->m_positional_arguments.empty())
        {
            auto& last_positional_argument = m_data_ptr->m_positional_arguments.back();
            if (!last_positional_argument->is_required())
            {
                throw configuration_exception("Optional positional argument already added");
            }
            if (last_positional_argument->get_max_count() > 1)
            {
                throw configuration_exception("Multivalue positional argument already added");
            }
        }

        auto new_argument = std::make_shared<positional_argument_type>(names, required, multivalue);

        add_to_names_repository(new_argument);

        m_data_ptr->m_positional_arguments.emplace_back(new_argument);

        return *new_argument;
    }

    results_type parse(int argc, char_type* argv[], values_storage_type& values_storage = get_null_storage())
    {
        return parse(argument_table_type(argc, argv), values_storage);
    }

    results_type parse(int argc, const char_type* argv[], values_storage_type& values_storage = get_null_storage())
    {
        return parse(argument_table_type(argc, argv), values_storage);
    }

    results_type parse(const argument_table_type& arg_table, values_storage_type& values_storage = get_null_storage())
    {
        auto results_data_ptr = std::make_shared<results_data_type>(m_data_ptr);

        results_data_ptr->set_app_name(arg_table.get_app_name());

        argument_table_iterator input_iterator(arg_table);

        parse_named_arguments(input_iterator, values_storage, results_data_ptr);
        parse_positional_arguments(input_iterator, values_storage, results_data_ptr);

        return results_type(results_data_ptr);
    }

private:
    using argument_table_iterator = basic_argument_table_iterator<TRAITS>;
    using argument_ptr_type = std::shared_ptr<argument_type>;
    using parser_data_type = internal::basic_parser_data<TRAITS, VALUES_STORAGE>;
    using parser_data_ptr_type = std::shared_ptr<parser_data_type>;
    using results_data_type = internal::basic_results_data<TRAITS>;
    using results_data_ptr_type = std::shared_ptr<results_data_type>;

    void parse_argument_value(const results_data_ptr_type& results_data_ptr, values_storage_type& values_storage,
        const argument_ptr_type& argument, const string_type& value_str)
    {
        // TODO: value checking (planned)

        auto count = results_data_ptr->value_count(argument);
        if (count >= argument->get_max_count())
        {
            throw parse_exception("Argument specified too many times");
        }

        results_data_ptr->append_value(argument, value_str);

        auto storage_handler = argument->get_storage_handler();
        if (storage_handler)
        {
            storage_handler->store(values_storage, value_str);
        }
    }

    bool parse_named_argument(const results_data_ptr_type& results_data_ptr, values_storage_type& values_storage,
        argument_table_iterator& input_iterator)
    {
        auto& input_value = input_iterator.peek_next();

        auto equal_char_pos = input_value.find(TRAITS::get_equal_literal());
        if (equal_char_pos == string_type::npos)
        {
            auto arg_iter = m_data_ptr->m_names_repository.find(input_value);
            if (arg_iter == m_data_ptr->m_names_repository.end())
            {
                // not an argument name, goto positional arguments processing
                return false;
            }

            auto& arg_object_ptr = arg_iter->second;

            string_type value_str;

            switch (arg_object_ptr->get_kind())
            {
            case argument_kind::VALUED:
                // argument found, so remove element from input
                input_iterator.take_next();

                if (!input_iterator.has_more())
                {
                    throw parse_exception("Value missing in input");
                }

                value_str = input_iterator.take_next();
                break;

            case argument_kind::SWITCH:
                // argument found, so remove element from input
                input_iterator.take_next();

                value_str = TRAITS::get_switch_enabled_literal();
                break;

            default:
                // not an option name, goto positional arguments processing
                return false;
            }

            parse_argument_value(results_data_ptr, values_storage, arg_object_ptr, value_str);
            return true;
        }
        else
        {
            auto name_str = input_value.substr(0, equal_char_pos);

            auto arg_iter = m_data_ptr->m_names_repository.find(name_str);
            if (arg_iter == m_data_ptr->m_names_repository.end())
            {
                return false;
            }

            auto value_str = input_value.substr(equal_char_pos + 1);

            auto& arg_object_ptr = arg_iter->second;

            switch (arg_object_ptr->get_kind())
            {
            case argument_kind::VALUED:
                // argument found, so remove element from input
                input_iterator.take_next();

                parse_argument_value(results_data_ptr, values_storage, arg_object_ptr, value_str);
                return true;

            case argument_kind::SWITCH:
                // argument found, so remove element from input
                input_iterator.take_next();

                throw parse_exception("Value specified for switch argument");

            default:
                // not an option name, goto positional arguments processing
                return false;
            }
        }
    }

    void parse_named_arguments(argument_table_iterator& input_iterator, values_storage_type& values_storage,
        const results_data_ptr_type& results_data_ptr)
    {
        while (input_iterator.has_more())
        {
            if (!parse_named_argument(results_data_ptr, values_storage, input_iterator))
            {
                break;
            }
        }
    }

    void parse_positional_arguments(argument_table_iterator& input_iterator, values_storage_type& values_storage,
        const results_data_ptr_type& results_data_ptr)
    {
        auto arg_iter = m_data_ptr->m_positional_arguments.begin();
        auto arg_end_iter = m_data_ptr->m_positional_arguments.end();

        while (input_iterator.has_more())
        {
            if (arg_iter == arg_end_iter)
            {
                throw parse_exception("Unexpected positional argument");
            }

            const auto& value_str = input_iterator.take_next();

            auto& arg_object_ptr = *arg_iter;

            parse_argument_value(results_data_ptr, values_storage, arg_object_ptr, value_str);

            if (arg_object_ptr->get_max_count() == 1)
            {
                ++arg_iter;
            }
        }

        for (; arg_iter != arg_end_iter; ++arg_iter)
        {
            auto& arg_object_ptr = *arg_iter;

            if (!arg_object_ptr->is_required())
            {
                continue;
            }

            if (results_data_ptr->value_count(arg_object_ptr) == 0)
            {
                throw parse_exception("Required positional argument missing");
            }
        }
    }

    void check_names(const string_vector_type& names)
    {
        auto name_count = names.size();

        /* check number of names */
        if (name_count < 1)
        {
            throw configuration_exception("No switch names given");
        }

        ensure_names_characters_valid(names);
        ensure_no_duplicated_names(names);
        ensure_names_not_registered(names);
    }

    void ensure_names_not_registered(const string_vector_type& names)
    {
        /* check if names are not already registered */
        for (const auto& name : names)
        {
            if (m_data_ptr->m_names_repository.find(name) != m_data_ptr->m_names_repository.end())
            {
                throw configuration_exception("Argument with given name already registered");
            }
        }
    }

    void ensure_no_duplicated_names(const string_vector_type& names)
    {
        auto name_count = names.size();

        for (std::size_t i = 0; i < name_count; ++i)
        {
            for (std::size_t j = i + 1; j < name_count; ++j)
            {
                if (names[i] == names[j])
                {
                    throw configuration_exception("Duplicated name");
                }
            }
        }
    }

    void add_to_names_repository(const std::shared_ptr<argument_type>& argument)
    {
        for (const auto& name : argument->get_names())
        {
            m_data_ptr->m_names_repository.emplace(name, argument);
        }
    }

    void ensure_names_characters_valid(const string_vector_type& names)
    {
        for (const auto& name : names)
        {
            ensure_name_characters_valid(name);
        }
    }

    void ensure_name_characters_valid(const string_type& name)
    {
        if (name.empty())
        {
            throw configuration_exception("Argument name must not be empty");
        }

        for (auto c : name)
        {
            if (internal::is_space(c))
            {
                throw configuration_exception("Argument name must not contain whitespace characters");
            }
            if (c == TRAITS::get_equal_literal())
            {
                throw configuration_exception("Argument name must not contain equal characters");
            }
        }
    }

    // TODO: try to refactor the way null storage is provided
    static values_storage_type& get_null_storage()
    {
        static values_storage_type null_storage;
        return null_storage;
    }

    const parser_data_ptr_type m_data_ptr;
};

} // namespace args
} // namespace oct

#endif /*OCTARGS_PARSER_HPP_*/