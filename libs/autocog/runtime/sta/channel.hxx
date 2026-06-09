#ifndef AUTOCOG_RUNTIME_STA_CHANNEL_HXX
#define AUTOCOG_RUNTIME_STA_CHANNEL_HXX

// Clause/Channel and their leaves are the shared autocog::data types now.
#include "autocog/data/channel.hxx"

namespace autocog::runtime::sta {
using BindClause   = ::autocog::data::BindClause;
using RavelClause  = ::autocog::data::RavelClause;
using WrapClause   = ::autocog::data::WrapClause;
using PruneClause  = ::autocog::data::PruneClause;
using MappedClause = ::autocog::data::MappedClause;
using Clause       = ::autocog::data::Clause;
using ChannelKwarg = ::autocog::data::ChannelKwarg;
using InputChannel    = ::autocog::data::InputChannel;
using DataflowChannel = ::autocog::data::DataflowChannel;
using CallChannel     = ::autocog::data::CallChannel;
using Channel         = ::autocog::data::Channel;
}

#endif // AUTOCOG_RUNTIME_STA_CHANNEL_HXX
