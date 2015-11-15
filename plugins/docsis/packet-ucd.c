/* packet-ucd.c
 * Routines for UCD Message dissection
 * Copyright 2002, Anand V. Narwani <anand[AT]narwani.org>
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <epan/packet.h>
#include <epan/exceptions.h>

#define UCD_SYMBOL_RATE 1
#define UCD_FREQUENCY 2
#define UCD_PREAMBLE 3
#define UCD_BURST_DESCR 4
#define UCD_BURST_DESCR5 5

#define UCD_MODULATION 1
#define UCD_DIFF_ENCODING 2
#define UCD_PREAMBLE_LEN 3
#define UCD_PREAMBLE_VAL_OFF 4
#define UCD_FEC 5
#define UCD_FEC_CODEWORD 6
#define UCD_SCRAMBLER_SEED 7
#define UCD_MAX_BURST 8
#define UCD_GUARD_TIME 9
#define UCD_LAST_CW_LEN 10
#define UCD_SCRAMBLER_ONOFF 11
#define UCD_RS_INT_DEPTH 12
#define UCD_RS_INT_BLOCK 13
#define UCD_PREAMBLE_TYPE 14

#define IUC_REQUEST 1
#define IUC_REQ_DATA 2
#define IUC_INIT_MAINT 3
#define IUC_STATION_MAINT 4
#define IUC_SHORT_DATA_GRANT 5
#define IUC_LONG_DATA_GRANT 6
#define IUC_NULL_IE 7
#define IUC_DATA_ACK 8
#define IUC_ADV_PHY_SHORT_DATA_GRANT 9
#define IUC_ADV_PHY_LONG_DATA_GRANT 10
#define IUC_ADV_PHY_UGS 11
#define IUC_RESERVED12 12
#define IUC_RESERVED13 13
#define IUC_RESERVED14 14
#define IUC_EXPANSION 15

void proto_register_docsis_ucd(void);
void proto_reg_handoff_docsis_ucd(void);

/* Initialize the protocol and registered fields */
static int proto_docsis_ucd = -1;

static int hf_docsis_ucd_upstream_chid = -1;
static int hf_docsis_ucd_config_ch_cnt = -1;
static int hf_docsis_ucd_mini_slot_size = -1;
static int hf_docsis_ucd_down_chid = -1;
static int hf_docsis_ucd_type = -1;
static int hf_docsis_ucd_length = -1;
static int hf_docsis_ucd_symbol_rate = -1;
static int hf_docsis_ucd_frequency = -1;
static int hf_docsis_ucd_preamble_pat = -1;
static int hf_docsis_ucd_iuc = -1;

static int hf_docsis_burst_mod_type = -1;
static int hf_docsis_burst_diff_encoding = -1;
static int hf_docsis_burst_preamble_len = -1;
static int hf_docsis_burst_preamble_val_off = -1;
static int hf_docsis_burst_fec = -1;
static int hf_docsis_burst_fec_codeword = -1;
static int hf_docsis_burst_scrambler_seed = -1;
static int hf_docsis_burst_max_burst = -1;
static int hf_docsis_burst_guard_time = -1;
static int hf_docsis_burst_last_cw_len = -1;
static int hf_docsis_burst_scrambler_onoff = -1;
static int hf_docsis_rs_int_depth = -1;
static int hf_docsis_rs_int_block = -1;
static int hf_docsis_preamble_type = -1;

/* Initialize the subtree pointers */
static gint ett_docsis_ucd = -1;
static gint ett_tlv = -1;

static const value_string channel_tlv_vals[] = {
  {UCD_SYMBOL_RATE,  "Symbol Rate"},
  {UCD_FREQUENCY,    "Frequency"},
  {UCD_PREAMBLE,     "Preamble Pattern"},
  {UCD_BURST_DESCR,  "Burst Descriptor"},
  {UCD_BURST_DESCR5, "Burst Descriptor DOCSIS 2.0"},
  {0, NULL}
};

static const value_string on_off_vals[] = {
  {1, "On"},
  {2, "Off"},
  {0, NULL}
};

static const value_string mod_vals[] = {
  {1, "QPSK"},
  {2, "QAM16"},
  {0, NULL}
};

value_string iuc_vals[] = {
  {IUC_REQUEST,                  "Request"},
  {IUC_REQ_DATA,                 "REQ/Data"},
  {IUC_INIT_MAINT,               "Initial Maintenance"},
  {IUC_STATION_MAINT,            "Station Maintenance"},
  {IUC_SHORT_DATA_GRANT,         "Short Data Grant"},
  {IUC_LONG_DATA_GRANT,          "Long Data Grant"},
  {IUC_NULL_IE,                  "NULL IE"},
  {IUC_DATA_ACK,                 "Data Ack"},
  {IUC_ADV_PHY_SHORT_DATA_GRANT, "Advanced Phy Short Data Grant"},
  {IUC_ADV_PHY_LONG_DATA_GRANT,  "Advanced Phy Long Data Grant"},
  {IUC_ADV_PHY_UGS,              "Advanced Phy UGS"},
  {IUC_RESERVED12,               "Reserved"},
  {IUC_RESERVED13,               "Reserved"},
  {IUC_RESERVED14,               "Reserved"},
  {IUC_EXPANSION,                "Expanded IUC"},
  {0, NULL}

};

static const value_string last_cw_len_vals[] = {
  {1, "Fixed"},
  {2, "Shortened"},
  {0, NULL}
};

/* Dissection */
static void
dissect_ucd (tvbuff_t * tvb, packet_info * pinfo, proto_tree * tree)
{
  int pos, endtlvpos;
  guint8 type, length;
  guint8 tlvlen, tlvtype;
  proto_tree *ucd_tree;
  proto_item *ucd_item;
  proto_tree *tlv_tree;
  proto_item *tlv_item;
  gint len;
  guint8 upchid, symrate;

  len = tvb_reported_length_remaining (tvb, 0);
  upchid = tvb_get_guint8 (tvb, 0);

  /* if the upstream Channel ID is 0 then this is for Telephony Return) */
  if (upchid > 0)
    col_add_fstr (pinfo->cinfo, COL_INFO,
                  "UCD Message:  Channel ID = %u (U%u)", upchid,
                  upchid - 1);
  else
    col_add_fstr (pinfo->cinfo, COL_INFO,
                  "UCD Message:  Channel ID = %u (Telephony Return)",
                  upchid);

  if (tree)
    {
      ucd_item =
        proto_tree_add_protocol_format (tree, proto_docsis_ucd, tvb, 0, -1,
                                        "UCD Message");
      ucd_tree = proto_item_add_subtree (ucd_item, ett_docsis_ucd);
      proto_tree_add_item (ucd_tree, hf_docsis_ucd_upstream_chid, tvb, 0, 1,
                           ENC_BIG_ENDIAN);
      proto_tree_add_item (ucd_tree, hf_docsis_ucd_config_ch_cnt, tvb, 1, 1,
                           ENC_BIG_ENDIAN);
      proto_tree_add_item (ucd_tree, hf_docsis_ucd_mini_slot_size, tvb, 2, 1,
                           ENC_BIG_ENDIAN);
      proto_tree_add_item (ucd_tree, hf_docsis_ucd_down_chid, tvb, 3, 1,
                           ENC_BIG_ENDIAN);

      pos = 4;
      while (pos < len)
        {
          type = tvb_get_guint8 (tvb, pos);
          tlv_tree = proto_tree_add_subtree(ucd_tree, tvb, pos, -1,
                                            ett_tlv, &tlv_item,
                                            val_to_str(type, channel_tlv_vals,
                                                       "Unknown TLV (%u)"));
          proto_tree_add_uint (tlv_tree, hf_docsis_ucd_type,
                               tvb, pos, 1, type);
          pos++;
          length = tvb_get_guint8 (tvb, pos);
          proto_tree_add_uint (tlv_tree, hf_docsis_ucd_length,
                               tvb, pos, 1, length);
          pos++;
          proto_item_set_len(tlv_item, length + 2);
          switch (type)
            {
              case UCD_SYMBOL_RATE:
                if (length == 1)
                  {
                    symrate = tvb_get_guint8 (tvb, pos);
                    proto_tree_add_uint (tlv_tree, hf_docsis_ucd_symbol_rate,
                                         tvb, pos, length, symrate * 160);
                  }
                else
                  {
                    expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                  }
                pos = pos + length;
                break;
              case UCD_FREQUENCY:
                if (length == 4)
                  {
                    proto_tree_add_item (tlv_tree, hf_docsis_ucd_frequency, tvb,
                                         pos, length, ENC_BIG_ENDIAN);
                    pos = pos + length;
                  }
                else
                  {
                    expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                  }
                break;
              case UCD_PREAMBLE:
                proto_tree_add_item (tlv_tree, hf_docsis_ucd_preamble_pat, tvb,
                                     pos, length, ENC_NA);
                pos = pos + length;
                break;
              case UCD_BURST_DESCR:
                proto_tree_add_item (tlv_tree, hf_docsis_ucd_iuc, tvb,
                                     pos++, 1, ENC_BIG_ENDIAN);
                endtlvpos = pos + length - 1;
                while (pos < endtlvpos)
                  {
                    tlvtype = tvb_get_guint8 (tvb, pos++);
                    tlvlen = tvb_get_guint8 (tvb, pos++);
                    switch (tlvtype)
                      {
                        case UCD_MODULATION:
                          if (tlvlen == 1)
                            {
                              proto_tree_add_item (tlv_tree,
                                                   hf_docsis_burst_mod_type, tvb,
                                                   pos, tlvlen, ENC_BIG_ENDIAN);
                            }
                          else
                            {
                              expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                            }
                          break;
                        case UCD_DIFF_ENCODING:
                          if (tlvlen == 1)
                            {
                              proto_tree_add_item (tlv_tree,
                                                   hf_docsis_burst_diff_encoding,
                                                   tvb, pos, tlvlen, ENC_BIG_ENDIAN);
                            }
                          else
                            {
                              expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                            }
                          break;
                        case UCD_PREAMBLE_LEN:
                          if (tlvlen == 2)
                            {
                              proto_tree_add_item (tlv_tree,
                                                   hf_docsis_burst_preamble_len,
                                                   tvb, pos, tlvlen, ENC_BIG_ENDIAN);
                            }
                          else
                            {
                              expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                            }
                          break;
                        case UCD_PREAMBLE_VAL_OFF:
                          if (tlvlen == 2)
                            {
                              proto_tree_add_item (tlv_tree,
                                                   hf_docsis_burst_preamble_val_off,
                                                   tvb, pos, tlvlen, ENC_BIG_ENDIAN);
                            }
                          else
                            {
                              expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                            }
                          break;
                        case UCD_FEC:
                          if (tlvlen == 1)
                            {
                              proto_tree_add_item (tlv_tree,
                                                   hf_docsis_burst_fec, tvb, pos,
                                                   tlvlen, ENC_BIG_ENDIAN);
                            }
                          else
                            {
                              expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                            }
                          break;
                        case UCD_FEC_CODEWORD:
                          if (tlvlen == 1)
                            {
                              proto_tree_add_item (tlv_tree,
                                                   hf_docsis_burst_fec_codeword,
                                                   tvb, pos, tlvlen, ENC_BIG_ENDIAN);
                            }
                          else
                            {
                              expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                            }
                          break;
                        case UCD_SCRAMBLER_SEED:
                          if (tlvlen == 2)
                            {
                              proto_tree_add_item (tlv_tree,
                                                   hf_docsis_burst_scrambler_seed,
                                                   tvb, pos, tlvlen, ENC_BIG_ENDIAN);
                            }
                          else
                            {
                              expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                            }
                          break;
                        case UCD_MAX_BURST:
                          if (tlvlen == 1)
                            {
                              proto_tree_add_item (tlv_tree,
                                                   hf_docsis_burst_max_burst, tvb,
                                                   pos, tlvlen, ENC_BIG_ENDIAN);
                            }
                          else
                            {
                              expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                            }
                          break;
                        case UCD_GUARD_TIME:
                          if (tlvlen == 1)
                            {
                              proto_tree_add_item (tlv_tree,
                                                   hf_docsis_burst_guard_time,
                                                   tvb, pos, tlvlen, ENC_BIG_ENDIAN);
                            }
                          else
                            {
                              expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                            }
                          break;
                        case UCD_LAST_CW_LEN:
                          if (tlvlen == 1)
                            {
                              proto_tree_add_item (tlv_tree,
                                                   hf_docsis_burst_last_cw_len,
                                                   tvb, pos, tlvlen, ENC_BIG_ENDIAN);
                            }
                          else
                            {
                              expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                            }
                          break;
                        case UCD_SCRAMBLER_ONOFF:
                          if (tlvlen == 1)
                            {
                              proto_tree_add_item (tlv_tree,
                                                   hf_docsis_burst_scrambler_onoff,
                                                   tvb, pos, tlvlen, ENC_BIG_ENDIAN);
                            }
                          else
                            {
                              expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                            }
                          break;
                      } /* switch(tlvtype) */
                    pos = pos + tlvlen;
                  } /* while (pos < endtlvpos) */
                break;
              case UCD_BURST_DESCR5:
                /* DOCSIS 2.0 Upstream Channel Descriptor */
                proto_tree_add_item (tlv_tree, hf_docsis_ucd_iuc, tvb,
                                     pos++, 1, ENC_BIG_ENDIAN);
                endtlvpos = pos + length - 1;
                while (pos < endtlvpos)
                  {
                    tlvtype = tvb_get_guint8 (tvb, pos++);
                    tlvlen = tvb_get_guint8 (tvb, pos++);
                    switch (tlvtype)
                      {
                        case UCD_MODULATION:
                          if (tlvlen == 1)
                            {
                              proto_tree_add_item (tlv_tree,
                                                   hf_docsis_burst_mod_type, tvb,
                                                   pos, tlvlen, ENC_BIG_ENDIAN);
                            }
                          else
                            {
                              expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                            }
                          break;
                        case UCD_DIFF_ENCODING:
                          if (tlvlen == 1)
                            {
                              proto_tree_add_item (tlv_tree,
                                                   hf_docsis_burst_diff_encoding,
                                                   tvb, pos, tlvlen, ENC_BIG_ENDIAN);
                            }
                          else
                            {
                              expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                            }
                          break;
                        case UCD_PREAMBLE_LEN:
                          if (tlvlen == 2)
                            {
                              proto_tree_add_item (tlv_tree,
                                                   hf_docsis_burst_preamble_len,
                                                   tvb, pos, tlvlen, ENC_BIG_ENDIAN);
                            }
                          else
                            {
                              expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                            }
                          break;
                        case UCD_PREAMBLE_VAL_OFF:
                          if (tlvlen == 2)
                            {
                              proto_tree_add_item (tlv_tree,
                                                   hf_docsis_burst_preamble_val_off,
                                                   tvb, pos, tlvlen, ENC_BIG_ENDIAN);
                            }
                          else
                            {
                              expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                            }
                          break;
                        case UCD_FEC:
                          if (tlvlen == 1)
                            {
                              proto_tree_add_item (tlv_tree,
                                                   hf_docsis_burst_fec, tvb, pos,
                                                   tlvlen, ENC_BIG_ENDIAN);
                            }
                          else
                            {
                              expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                            }
                          break;
                        case UCD_FEC_CODEWORD:
                          if (tlvlen == 1)
                            {
                              proto_tree_add_item (tlv_tree,
                                                   hf_docsis_burst_fec_codeword,
                                                   tvb, pos, tlvlen, ENC_BIG_ENDIAN);
                            }
                          else
                            {
                              expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                            }
                          break;
                        case UCD_SCRAMBLER_SEED:
                          if (tlvlen == 2)
                            {
                              proto_tree_add_item (tlv_tree,
                                                   hf_docsis_burst_scrambler_seed,
                                                   tvb, pos, tlvlen, ENC_BIG_ENDIAN);
                            }
                          else
                            {
                              expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                            }
                          break;
                        case UCD_MAX_BURST:
                          if (tlvlen == 1)
                            {
                              proto_tree_add_item (tlv_tree,
                                                   hf_docsis_burst_max_burst, tvb,
                                                   pos, tlvlen, ENC_BIG_ENDIAN);
                            }
                          else
                            {
                              expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                            }
                          break;
                        case UCD_GUARD_TIME:
                          if (tlvlen == 1)
                            {
                              proto_tree_add_item (tlv_tree,
                                                   hf_docsis_burst_guard_time,
                                                   tvb, pos, tlvlen, ENC_BIG_ENDIAN);
                            }
                          else
                            {
                              expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                            }
                          break;
                        case UCD_LAST_CW_LEN:
                          if (tlvlen == 1)
                            {
                              proto_tree_add_item (tlv_tree,
                                                   hf_docsis_burst_last_cw_len,
                                                   tvb, pos, tlvlen, ENC_BIG_ENDIAN);
                            }
                          else
                            {
                              expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                            }
                          break;
                        case UCD_SCRAMBLER_ONOFF:
                          if (tlvlen == 1)
                            {
                              proto_tree_add_item (tlv_tree,
                                                   hf_docsis_burst_scrambler_onoff,
                                                   tvb, pos, tlvlen, ENC_BIG_ENDIAN);
                            }
                          else
                            {
                              expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                            }
                          break;
                        case UCD_RS_INT_DEPTH:
                          if (tlvlen == 1)
                            {
                              proto_tree_add_item (tlv_tree,
                                                   hf_docsis_rs_int_depth,
                                                   tvb, pos, tlvlen, ENC_BIG_ENDIAN);
                            }
                          else
                            {
                              expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                            }
                          break;
                        case UCD_RS_INT_BLOCK:
                          if (tlvlen == 2)
                            {
                              proto_tree_add_item (tlv_tree,
                                                   hf_docsis_rs_int_block,
                                                   tvb, pos, tlvlen, ENC_BIG_ENDIAN);
                            }
                          else
                            {
                              expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                            }
                          break;
                        case UCD_PREAMBLE_TYPE:
                          if (tlvlen == 1)
                            {
                              proto_tree_add_item (tlv_tree,
                                                   hf_docsis_preamble_type,
                                                   tvb, pos, tlvlen, ENC_BIG_ENDIAN);
                            }
                          else
                            {
                              expert_add_info_format(pinfo, ucd_item, &ei_docsis_ucd_tlvlen_bad, "Wrong TLV length: %u", length);
                            }
                          break;
                      }           /* switch(tlvtype) */
                    pos = pos + tlvlen;
                  }               /* while (pos < endtlvpos) */
                break;
            }                   /* switch(type) */
        }                       /* while (pos < len) */
    }                           /* if (tree) */
}

/* Register the protocol with Wireshark */
void
proto_register_docsis_ucd (void)
{
  static hf_register_info hf[] = {
    {&hf_docsis_ucd_upstream_chid,
     {"Upstream Channel ID", "docsis_ucd.upchid",
      FT_UINT8, BASE_DEC, NULL, 0x0,
      NULL, HFILL}
    },
    {&hf_docsis_ucd_config_ch_cnt,
     {"Config Change Count", "docsis_ucd.confcngcnt",
      FT_UINT8, BASE_DEC, NULL, 0x0,
      "Configuration Change Count", HFILL}
    },
    {&hf_docsis_ucd_mini_slot_size,
     {"Mini Slot Size (6.25us TimeTicks)", "docsis_ucd.mslotsize",
      FT_UINT8, BASE_DEC, NULL, 0x0,
      NULL, HFILL}
    },
    {&hf_docsis_ucd_down_chid,
     {"Downstream Channel ID", "docsis_ucd.downchid",
      FT_UINT8, BASE_DEC, NULL, 0x0,
      "Management Message", HFILL}
    },
    {&hf_docsis_ucd_type,
     {"TLV Type", "docsis_ucd.type",
      FT_UINT8, BASE_DEC, VALS(channel_tlv_vals), 0x0,
      "Channel TLV type", HFILL}
    },
    {&hf_docsis_ucd_length,
     {"TLV Length", "docsis_ucd.length",
      FT_UINT8, BASE_DEC, NULL, 0x0,
      "Channel TLV length", HFILL}
    },
    {&hf_docsis_ucd_symbol_rate,
     {"Symbol Rate (ksym/sec)", "docsis_ucd.symrate",
      FT_UINT8, BASE_DEC, NULL, 0x0,
      "Symbol Rate", HFILL}
    },
    {&hf_docsis_ucd_frequency,
     {"Frequency (Hz)", "docsis_ucd.freq",
      FT_UINT32, BASE_DEC, NULL, 0x0,
      "Upstream Center Frequency", HFILL}
    },
    {&hf_docsis_ucd_preamble_pat,
     {"Preamble Pattern", "docsis_ucd.preamble",
      FT_BYTES, BASE_NONE, NULL, 0x0,
      "Preamble Superstring", HFILL}
    },
    {&hf_docsis_ucd_iuc,
     {"Interval Usage Code", "docsis_ucd.iuc",
      FT_UINT8, BASE_DEC, VALS (iuc_vals), 0x0,
      NULL, HFILL}
    },
    {&hf_docsis_burst_mod_type,
     {"1 Modulation Type", "docsis_ucd.burst.modtype",
      FT_UINT8, BASE_DEC, VALS (mod_vals), 0x0,
      "Modulation Type", HFILL}
    },
    {&hf_docsis_burst_diff_encoding,
     {"2 Differential Encoding", "docsis_ucd.burst.diffenc",
      FT_UINT8, BASE_DEC, VALS (on_off_vals), 0x0,
      "Differential Encoding", HFILL}
    },
    {&hf_docsis_burst_preamble_len,
     {"3 Preamble Length (Bits)", "docsis_ucd.burst.preamble_len",
      FT_UINT16, BASE_DEC, NULL, 0x0,
      "Preamble Length (Bits)", HFILL}
    },
    {&hf_docsis_burst_preamble_val_off,
     {"4 Preamble Offset (Bits)", "docsis_ucd.burst.preamble_off",
      FT_UINT16, BASE_DEC, NULL, 0x0,
      "Preamble Offset (Bits)", HFILL}
    },
    {&hf_docsis_burst_fec,
     {"5 FEC (T)", "docsis_ucd.burst.fec",
      FT_UINT8, BASE_DEC, NULL, 0x0,
      "FEC (T) Codeword Parity Bits = 2^T", HFILL}
    },
    {&hf_docsis_burst_fec_codeword,
     {"6 FEC Codeword Info bytes (k)", "docsis_ucd.burst.fec_codeword",
      FT_UINT8, BASE_DEC, NULL, 0x0,
      "FEC Codeword Info Bytes (k)", HFILL}
    },
    {&hf_docsis_burst_scrambler_seed,
     {"7 Scrambler Seed", "docsis_ucd.burst.scrambler_seed",
      FT_UINT16, BASE_HEX, NULL, 0x0,
      "Burst Descriptor", HFILL}
    },
    {&hf_docsis_burst_max_burst,
     {"8 Max Burst Size (Minislots)", "docsis_ucd.burst.maxburst",
      FT_UINT8, BASE_DEC, NULL, 0x0,
      "Max Burst Size (Minislots)", HFILL}
    },
    {&hf_docsis_burst_guard_time,
     {"9 Guard Time Size (Symbol Times)", "docsis_ucd.burst.guardtime",
      FT_UINT8, BASE_DEC, NULL, 0x0,
      "Guard Time Size", HFILL}
    },
    {&hf_docsis_burst_last_cw_len,
     {"10 Last Codeword Length", "docsis_ucd.burst.last_cw_len",
      FT_UINT8, BASE_DEC, VALS (last_cw_len_vals), 0x0,
      "Last Codeword Length", HFILL}
    },
    {&hf_docsis_burst_scrambler_onoff,
     {"11 Scrambler On/Off", "docsis_ucd.burst.scrambleronoff",
      FT_UINT8, BASE_DEC, VALS (on_off_vals), 0x0,
      "Scrambler On/Off", HFILL}
    },
    {&hf_docsis_rs_int_depth,
     {"12 RS Interleaver Depth", "docsis_ucd.burst.rsintdepth",
      FT_UINT8, BASE_DEC, NULL, 0x0,
      "R-S Interleaver Depth", HFILL}
    },
    {&hf_docsis_rs_int_block,
     {"13 RS Interleaver Block Size", "docsis_ucd.burst.rsintblock",
      FT_UINT8, BASE_DEC, NULL, 0x0,
      "R-S Interleaver Block", HFILL}
    },
    {&hf_docsis_preamble_type,
     {"14 Preamble Type", "docsis_ucd.burst.preambletype",
      FT_UINT8, BASE_DEC, NULL, 0x0,
      "Preamble Type", HFILL}
    },
  };

  static gint *ett[] = {
    &ett_docsis_ucd,
    &ett_tlv,
  };

  proto_docsis_ucd =
    proto_register_protocol ("DOCSIS Upstream Channel Descriptor",
                             "DOCSIS UCD", "docsis_ucd");

  proto_register_field_array (proto_docsis_ucd, hf, array_length (hf));
  proto_register_subtree_array (ett, array_length (ett));

  register_dissector ("docsis_ucd", dissect_ucd, proto_docsis_ucd);
}

void
proto_reg_handoff_docsis_ucd (void)
{
   dissector_handle_t docsis_ucd_handle;

   docsis_ucd_handle = find_dissector ("docsis_ucd");
   dissector_add_uint ("docsis_mgmt", 0x02, docsis_ucd_handle);
}

/*
 * Editor modelines  -  http://www.wireshark.org/tools/modelines.html
 *
 * Local Variables:
 * c-basic-offset: 2
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * ex: set shiftwidth=2 tabstop=8 expandtab:
 * :indentSize=2:tabSize=8:noTabs=true:
 */
