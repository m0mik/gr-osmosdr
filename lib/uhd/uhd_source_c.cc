/* -*- c++ -*- */
/*
 * Copyright 2012 Dimitri Stolnikov <horiz0n@gmx.net>
 *
 * GNU Radio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * GNU Radio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include <boost/foreach.hpp>
#include <boost/assign.hpp>
#include <boost/algorithm/string.hpp>

//#include <uhd/property_tree.hpp>

#include "arg_helpers.h"

#include "uhd_source_c.h"
#include "osmosdr/source.h"

using namespace boost::assign;

uhd_source_c_sptr make_uhd_source_c(const std::string &args)
{
  return gnuradio::get_initial_sptr(new uhd_source_c(args));
}

uhd_source_c::uhd_source_c(const std::string &args) :
    gr::hier_block2("uhd_source_c",
                   gr::io_signature::make(0, 0, 0),
                   args_to_io_signature(args)),
    _center_freq(0.0f),
    _freq_corr(0.0f),
    _lo_offset(0.0f),
    _clock_source("")
{
  size_t nchan = 1;
  dict_t dict = params_to_dict(args);

  if (dict.count("nchan"))
    nchan = boost::lexical_cast< size_t >( dict["nchan"] );

  if (0 == nchan)
    nchan = 1;

  if (dict.count("lo_offset"))
    _lo_offset = boost::lexical_cast< double >( dict["lo_offset"] );

  if (dict.count("clock_source"))
    _clock_source = boost::lexical_cast< std::string >( dict["clock_source"] );

  std::string arguments; // rebuild argument string without internal arguments
  BOOST_FOREACH( dict_t::value_type &entry, dict ) {
    if ( "uhd" != entry.first &&
         "nchan" != entry.first &&
         "subdev" != entry.first &&
         "lo_offset" != entry.first &&
         "clock_source" != entry.first ) {
      arguments += entry.first + "=" + entry.second + ",";
    }
  }

  _src = gr::uhd::usrp_source::make( arguments,
                                     uhd::io_type_t::COMPLEX_FLOAT32,
                                     nchan );

  if (dict.count("subdev")) {
    _src->set_subdev_spec( dict["subdev"] );
  }

  std::cerr << "-- Using subdev spec '" << _src->get_subdev_spec() << "'."
            << std::endl;

  if (0.0 != _lo_offset)
    std::cerr << "-- Using lo offset of " << _lo_offset << " Hz." << std::endl;

  if ("" != _clock_source)
    std::cerr << "-- Using clock source " << _clock_source << "." << std::endl;

  for ( size_t i = 0; i < nchan; i++ )
    connect( _src, i, self(), i );
}

uhd_source_c::~uhd_source_c()
{
}

std::vector< std::string > uhd_source_c::get_devices()
{
  std::vector< std::string > devices;

  uhd::device_addr_t hint;
  BOOST_FOREACH(const uhd::device_addr_t &dev, uhd::device::find(hint))
  {
    std::string args = "uhd," + dev.to_string();

    std::string type = dev.cast< std::string >("type", "usrp");
    std::string name = dev.cast< std::string >("name", "");
    std::string serial = dev.cast< std::string >("serial", "");

    std::string label = "Ettus";

    if ( "umtrx" == type )
      label = "Fairwaves";

    if (type.length()) {
      boost::to_upper(type);
      label += " " + type;
    }

    if (name.length())
      label += " (" + name + ")";

    if (serial.length())
      label += " " + serial;

    args += ",label='" + label +  + "'";

    devices.push_back( args );
  }

  return devices;
}

std::string uhd_source_c::name()
{
//  uhd::property_tree::sptr prop_tree = _src->get_device()->get_device()->get_tree();
//  std::string dev_name = prop_tree->access<std::string>("/name").get();
  std::string mboard_name = _src->get_device()->get_mboard_name();

//  std::cerr << "'" << dev_name << "' '" << mboard_name << "'" << std::endl;
//  'USRP1 Device' 'USRP1 (Classic)'
//  'B-Series Device' 'B100 (B-Hundo)'

  return mboard_name;
}

size_t uhd_source_c::get_num_channels()
{
  return _src->get_device()->get_rx_num_channels();
}

osmosdr::meta_range_t uhd_source_c::get_sample_rates( void )
{
  osmosdr::meta_range_t rates;

  BOOST_FOREACH( uhd::range_t rate, _src->get_samp_rates() )
      rates += osmosdr::range_t( rate.start(), rate.stop(), rate.step() );

  return rates;
}

double uhd_source_c::set_sample_rate( double rate )
{
  _src->set_samp_rate( rate );
  return get_sample_rate();
}

double uhd_source_c::get_sample_rate( void )
{
  return _src->get_samp_rate();
}

osmosdr::freq_range_t uhd_source_c::get_freq_range( size_t chan )
{
  osmosdr::freq_range_t range;

  BOOST_FOREACH( uhd::range_t freq, _src->get_freq_range(chan) )
      range += osmosdr::range_t( freq.start(), freq.stop(), freq.step() );

  return range;
}

double uhd_source_c::set_center_freq( double freq, size_t chan )
{
  #define APPLY_PPM_CORR(val, ppm) ((val) * (1.0 + (ppm) * 0.000001))

  double corr_freq = APPLY_PPM_CORR( freq, _freq_corr );

  // advanced tuning with tune_request_t
  uhd::tune_request_t tune_req(corr_freq, _lo_offset);
  _src->set_center_freq(tune_req, chan);

  _center_freq = freq;

  return get_center_freq(chan);
}

double uhd_source_c::get_center_freq( size_t chan )
{
  return _src->get_center_freq(chan);
}

std::string uhd_source_c::set_clock_source( const std::string & clock_source, size_t mboard )
{
  _src->set_clock_source(clock_source, mboard);
  std::cerr << "_src->set_clock_source() = " << clock_source << std::endl;

  return get_clock_source(mboard);
}

std::string uhd_source_c::get_clock_source( size_t mboard )
{
  return _src->get_clock_source(mboard);
}

double uhd_source_c::set_freq_corr( double ppm, size_t chan )
{
  _freq_corr = ppm;

  set_center_freq( _center_freq );

  return get_freq_corr( chan );
}

double uhd_source_c::get_freq_corr( size_t chan )
{
  return _freq_corr;
}

std::vector<std::string> uhd_source_c::get_gain_names( size_t chan )
{
  return _src->get_gain_names( chan );
}

osmosdr::gain_range_t uhd_source_c::get_gain_range( size_t chan )
{
  osmosdr::gain_range_t range;

  BOOST_FOREACH( uhd::range_t gain, _src->get_gain_range(chan) )
      range += osmosdr::range_t( gain.start(), gain.stop(), gain.step() );

  return range;
}

osmosdr::gain_range_t uhd_source_c::get_gain_range( const std::string & name, size_t chan )
{
  osmosdr::gain_range_t range;

  BOOST_FOREACH( uhd::range_t gain, _src->get_gain_range(name, chan) )
      range += osmosdr::range_t( gain.start(), gain.stop(), gain.step() );

  return range;
}

double uhd_source_c::set_gain( double gain, size_t chan )
{
  _src->set_gain(gain, chan);

  return get_gain(chan);
}

double uhd_source_c::set_gain( double gain, const std::string & name, size_t chan )
{
  _src->set_gain(gain, name, chan);

  return get_gain(name, chan);
}

double uhd_source_c::get_gain( size_t chan )
{
  return _src->get_gain(chan);
}

double uhd_source_c::get_gain( const std::string & name, size_t chan )
{
  return _src->get_gain(name, chan);
}

std::vector< std::string > uhd_source_c::get_antennas( size_t chan )
{
  return _src->get_antennas(chan);
}

std::string uhd_source_c::set_antenna( const std::string & antenna, size_t chan )
{
  _src->set_antenna(antenna, chan);

  return _src->get_antenna(chan);
}

std::string uhd_source_c::get_antenna( size_t chan )
{
  return _src->get_antenna(chan);
}

void uhd_source_c::set_dc_offset_mode( int mode, size_t chan )
{
  if ( osmosdr::source::DCOffsetOff == mode ) {
    _src->set_auto_dc_offset( false, chan );
    _src->set_dc_offset( std::complex<double>(0.0, 0.0), chan ); /* uhd default */
  } else if ( osmosdr::source::DCOffsetManual == mode ) {
    _src->set_auto_dc_offset( false, chan );
  } else if ( osmosdr::source::DCOffsetAutomatic == mode ) {
    _src->set_auto_dc_offset( true, chan );
  }
}

void uhd_source_c::set_dc_offset( const std::complex<double> &offset, size_t chan )
{
  _src->set_dc_offset( offset, chan );
}

void uhd_source_c::set_iq_balance_mode( int mode, size_t chan )
{
  if ( osmosdr::source::IQBalanceOff == mode ) {
    _src->set_iq_balance( std::complex<double>(0.0, 0.0), chan ); /* uhd default */
  } else if ( osmosdr::source::IQBalanceManual == mode ) {
    /* nothing to do */
  } else if ( osmosdr::source::IQBalanceAutomatic == mode ) {
    throw std::runtime_error("Automatic IQ imbalance correction not implemented");
  }
}

void uhd_source_c::set_iq_balance( const std::complex<double> &balance, size_t chan )
{
  _src->set_iq_balance( balance, chan );
}

double uhd_source_c::set_bandwidth( double bandwidth, size_t chan )
{
  _src->set_bandwidth(bandwidth, chan);

  return _src->get_bandwidth(chan);
}

double uhd_source_c::get_bandwidth( size_t chan )
{
  return _src->get_bandwidth(chan);
}

osmosdr::freq_range_t uhd_source_c::get_bandwidth_range( size_t chan )
{
  osmosdr::freq_range_t bandwidths;

  BOOST_FOREACH( uhd::range_t bw, _src->get_bandwidth_range(chan) )
      bandwidths += osmosdr::range_t( bw.start(), bw.stop(), bw.step() );

  return bandwidths;
}
