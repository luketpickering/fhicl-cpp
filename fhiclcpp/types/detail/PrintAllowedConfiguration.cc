#include "cetlib/container_algorithms.h"
#include "cetlib/trim.h"
#include "fhiclcpp/detail/printing_helpers.h"
#include "fhiclcpp/types/detail/AtomBase.h"
#include "fhiclcpp/types/detail/ParameterBase.h"
#include "fhiclcpp/types/detail/SequenceBase.h"
#include "fhiclcpp/types/detail/SeqVectorBase.h"
#include "fhiclcpp/types/detail/TableBase.h"
#include "fhiclcpp/types/detail/PrintAllowedConfiguration.h"

#include <iomanip>
#include <regex>

namespace {

  inline bool is_sequence_element(std::string const& k)
  {
    auto pos = k.find_last_of("]");
    return pos != std::string::npos && pos == k.size()-1;
  }

  using namespace fhicl::detail;

  struct maybeName {

    maybeName(ParameterBase const* p, std::string const& ind)
      : is_seq_elem{is_sequence_element(p->key())}
      , name{p->name()}
      , indent{ind}
    {}

    bool is_seq_elem;
    std::string name;
    std::string indent;
  };

  std::ostream& operator<<(std::ostream& os, maybeName && mn)
  {
    if ( !mn.is_seq_elem ) {
      os << mn.indent << mn.name << ": ";
    }
    else {
      os << mn.indent;
    }
    return os;
  }

  std::string suffix (std::unordered_set<std::string>& keysWithCommas_,
                      std::unordered_set<std::string>& keysWithEllipses_,
                      std::string const& key,
                      std::string const& indent)
  {
    std::string result;
    if (cet::search_all(keysWithCommas_, key)) {
      keysWithCommas_.erase(key);
      result += ",";
    }
    if (cet::search_all(keysWithEllipses_, key)) {
      keysWithEllipses_.erase(key);
      result.append("\n").append(indent).append("...");
    }
    return result;
  }

  std::string non_whitespace(std::string const& s, std::size_t const sz)
  {

    // To support denoting optional tables, we sometimes print an
    // indentation string that has only one character in it.  But we
    // don't want to print any extra whitespace.  So we remove it
    // here.
    //
    // We also ignore indents if the size is one -- i.e. the current
    // state of the indent stack does not include any nested
    // parameters, so there's no reason to include any prefixes that
    // might be on the lowest-level indent (corresponding to sz == 1).

    return sz > 1 ? cet::trim_right_copy(s," ") : "";
  }

}

using namespace fhicl::detail;

//======================================================================

bool
PrintAllowedConfiguration::before_action(ParameterBase const* p)
{
  if ( !p->comment().empty() ) {
    buffer_ << non_whitespace(indent_(), indent_.size()) << '\n';
    for(auto const& line : cet::split_by_regex(p->comment(), "\n"))
      buffer_ << indent_() << "# " << line << '\n';
  }

  if ( !is_sequence_element(p->key()) ) {
    buffer_ << non_whitespace(indent_(), indent_.size()) << '\n';

    // In general, optional parameters cannot be template arguments to
    // sequences.  However, the implementation for 'TupleAs' uses
    // OptionalTuple<...> as the holding type of the sequence
    // elements.  In the case where we have Sequence< TupleAs<> >, the
    // TupleAs entries will be prefaced with '(', and we don't want
    // that.  Therefore, we modify the top indentation fragment only
    // if the parameter is not a sequence element.

    if ( p->is_optional() ) {
      indent_.modify_top(" ( ");
    }
  }

  mps_.emplace(p, showParentsForFirstParam_, indent_);
  showParentsForFirstParam_ = false;

  buffer_ << mps_.top().parent_names();

  return true;
}

void
PrintAllowedConfiguration::after_action(ParameterBase const* p)
{
  buffer_ << suffix( keysWithCommas_,
                     keysWithEllipses_,
                     p->key(),
                     indent_() );

  if ( p->has_default() && p->parameter_type() == par_type::ATOM )
    buffer_ << "  # default";

  if ( p->is_optional() ) {
    indent_.modify_top(std::string(3,' '));
  }

  buffer_ << '\n'
          << mps_.top().closing_braces();
  mps_.pop();
}

//======================================================================

void
PrintAllowedConfiguration::enter_table(TableBase const* t)
{
  buffer_ << maybeName{t, indent_()} << "{\n";
  indent_.push();
}

void
PrintAllowedConfiguration::exit_table(TableBase const*)
{
  indent_.pop();
  buffer_ << indent_() << "}";
}

//======================================================================

void
PrintAllowedConfiguration::enter_sequence(SequenceBase const* s)
{
  buffer_ << maybeName{s, indent_()} << "[\n";

  indent_.push();

  if ( s->elements().empty() )
    return;

  // We want the printout to look like this for sequences with
  // defaults:
  //
  //   list1: [
  //     1,  # default
  //     2   # default
  //   ]
  //
  // And like this for vectors w/o defaults (has length 1):
  //
  //   list2: [
  //      <int>,
  //      ...
  //   ]

  if ( s->has_default() || (s->parameter_type() != par_type::SEQ_VECTOR) ) {
    auto const& elements = s->elements();
    std::transform( elements.cbegin(), elements.cend()-1,
                    std::inserter(keysWithCommas_, keysWithCommas_.begin()),
                    [](auto e){ return e->key(); } );
    return;
  }

  if ( s->elements().size() != 1ul ) {
    throw cet::exception("LogicError") <<
      "Default Sequence<T> has a size not equal to 1.\n"
      "Maybe you tried to print the allowed configuration\n"
      "after the provided ParameterSet had been validated.\n"
      "If not, please notify the artists.";
  }

  keysWithCommas_.emplace(s->elements()[0]->key());
  keysWithEllipses_.emplace(s->elements()[0]->key());
}

void
PrintAllowedConfiguration::exit_sequence(SequenceBase const*)
{
  indent_.pop();
  buffer_ << indent_() << "]";
}

//======================================================================

void
PrintAllowedConfiguration::atom(AtomBase const* a)
{
  buffer_ << maybeName{a, indent_()}
          << a->stringified_value();
}
