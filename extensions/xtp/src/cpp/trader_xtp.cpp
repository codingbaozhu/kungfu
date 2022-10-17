//
// Created by qlu on 2019/2/11.
//

#include <algorithm>

#include "trader_xtp.h"
#include "type_convert.h"

namespace kungfu::wingchun::xtp {
using namespace kungfu::yijinjing::data;
struct TDConfiguration {
  int client_id;
  std::string account_id;
  std::string password;
  std::string software_key;
  std::string td_ip;
  int td_port;
};

void from_json(const nlohmann::json &j, TDConfiguration &c) {
  j.at("client_id").get_to(c.client_id);
  j.at("account_id").get_to(c.account_id);
  j.at("password").get_to(c.password);
  j.at("software_key").get_to(c.software_key);
  j.at("td_ip").get_to(c.td_ip);
  j.at("td_port").get_to(c.td_port);
}

TraderXTP::TraderXTP(broker::BrokerVendor &vendor) : Trader(vendor), session_id_(0), request_id_(0), trading_day_("") {
  KUNGFU_SETUP_LOG();
}

TraderXTP::~TraderXTP() {
  if (api_ != nullptr) {
    api_->Release();
  }
}

void TraderXTP::on_start() {
  TDConfiguration config = nlohmann::json::parse(get_config());
  if (config.client_id < 1 or config.client_id > 99) {
    throw wingchun_error("client_id must between 1 and 99");
  }
  std::string runtime_folder = get_runtime_folder();
  SPDLOG_INFO("Connecting XTP account {} with tcp://{}:{}", config.account_id, config.td_ip, config.td_port);
  api_ = XTP::API::TraderApi::CreateTraderApi(config.client_id, runtime_folder.c_str());
  api_->RegisterSpi(this);
  api_->SubscribePublicTopic(XTP_TERT_QUICK);
  api_->SetSoftwareVersion("1.1.0");
  api_->SetSoftwareKey(config.software_key.c_str());
  session_id_ = api_->Login(config.td_ip.c_str(), config.td_port, config.account_id.c_str(), config.password.c_str(),
                            XTP_PROTOCOL_TCP);
  if (session_id_ > 0) {
    update_broker_state(BrokerState::Ready);
    SPDLOG_INFO("Login successfully");
  } else {
    update_broker_state(BrokerState::LoginFailed);
    SPDLOG_ERROR("Login failed [{}]: {}", api_->GetApiLastError()->error_id, api_->GetApiLastError()->error_msg);
  }
}

void TraderXTP::on_exit() {
  if (api_ != nullptr and session_id_ > 0) {
    auto result = api_->Logout(session_id_);
    SPDLOG_INFO("Logout with return code {}", result);
  }
}

void TraderXTP::on_trading_day(const event_ptr &event, int64_t daytime) {
  trading_day_ = yijinjing::time::strftime(daytime, KUNGFU_TRADING_DAY_FORMAT);
}

bool TraderXTP::insert_order(const event_ptr &event) {
  const OrderInput &input = event->data<OrderInput>();
  XTPOrderInsertInfo xtp_input = {};
  to_xtp(xtp_input, input);

  uint64_t xtp_order_id = api_->InsertOrder(&xtp_input, session_id_);
  auto success = xtp_order_id != 0;

  auto nano = yijinjing::time::now_in_nano();
  auto writer = get_writer(event->source());
  Order &order = writer->open_data<Order>(event->gen_time());
  order_from_input(input, order);
  strncpy(order.trading_day, trading_day_.c_str(), DATE_LEN);
  order.insert_time = nano;
  order.update_time = nano;

  if (success) {
    outbound_orders_.emplace(input.order_id, xtp_order_id);
    inbound_orders_.emplace(xtp_order_id, input.order_id);
  } else {
    auto error_info = api_->GetApiLastError();
    order.error_id = error_info->error_id;
    strncpy(order.error_msg, error_info->error_msg, ERROR_MSG_LEN);
    order.status = OrderStatus::Error;
  }

  orders_.emplace(order.uid(), state<Order>(event->dest(), event->source(), nano, order));
  writer->close_data();
  if (not success) {
    SPDLOG_ERROR("fail to insert order {}, error id {}, {}", to_string(xtp_input), (int)order.error_id,
                 order.error_msg);
  }
  return success;
}

bool TraderXTP::cancel_order(const event_ptr &event) {
  const OrderAction &action = event->data<OrderAction>();
  if (outbound_orders_.find(action.order_id) == outbound_orders_.end()) {
    SPDLOG_ERROR("failed to cancel order {}, can't find related xtp order id", action.order_id);
    return false;
  }
  uint64_t xtp_order_id = outbound_orders_.at(action.order_id);
  auto order_state = orders_.at(action.order_id);
  auto xtp_action_id = api_->CancelOrder(xtp_order_id, session_id_);
  auto success = xtp_action_id != 0;
  if (not success) {
    XTPRI *error_info = api_->GetApiLastError();
    SPDLOG_ERROR("failed to cancel order {}, order_xtp_id: {} session_id: {} error_id: {} error_msg: {}",
                 action.order_id, xtp_order_id, session_id_, error_info->error_id, error_info->error_msg);
  }
  return success;
}

bool TraderXTP::req_position() {
  SPDLOG_INFO("req_position");
  return api_->QueryPosition(nullptr, this->session_id_, ++request_id_) == 0;
}

bool TraderXTP::req_account() {
  SPDLOG_INFO("req_account");
  return api_->QueryAsset(session_id_, ++request_id_) == 0;
}

void TraderXTP::OnDisconnected(uint64_t session_id, int reason) {
  if (session_id == session_id_) {
    update_broker_state(BrokerState::DisConnected);
    SPDLOG_ERROR("disconnected, reason: {}", reason);
  }
}

void TraderXTP::OnOrderEvent(XTPOrderInfo *order_info, XTPRI *error_info, uint64_t session_id) {
  if (inbound_orders_.find(order_info->order_xtp_id) == inbound_orders_.end()) {
    SPDLOG_ERROR("unrecognized xtp_order_id {}@{}", order_info->order_xtp_id, trading_day_);
    return;
  }
  auto is_error = error_info != nullptr and error_info->error_id != 0;
  auto order_id = inbound_orders_.at(order_info->order_xtp_id);
  auto order_state = orders_.at(order_id);
  auto writer = get_writer(order_state.dest);
  Order &order = writer->open_data<Order>(0);
  memcpy(&order, &(order_state.data), sizeof(order));
  from_xtp(*order_info, order);
  order.update_time = yijinjing::time::now_in_nano();
  if (is_error) {
    order.error_id = error_info->error_id;
    strncpy(order.error_msg, error_info->error_msg, ERROR_MSG_LEN);
  }
  writer->close_data();
}

void TraderXTP::OnTradeEvent(XTPTradeReport *trade_info, uint64_t session_id) {
  if (inbound_orders_.find(trade_info->order_xtp_id) == inbound_orders_.end()) {
    SPDLOG_ERROR("unrecognized xtp_order_id {}", trade_info->order_xtp_id);
    return;
  }
  auto order_id = inbound_orders_.at(trade_info->order_xtp_id);
  auto &order_state = orders_.at(order_id);
  auto writer = get_writer(order_state.dest);
  Trade &trade = writer->open_data<Trade>(now());
  from_xtp(*trade_info, trade);
  trade.trade_id = writer->current_frame_uid();
  trade.order_id = order_id;
  trade.trade_time = yijinjing::time::now_in_nano();
  strcpy(trade.trading_day, trading_day_.c_str());
  trade.instrument_type = get_instrument_type(trade.exchange_id, trade.instrument_id);
  writer->close_data();
  order_state.data.volume_left -= trade.volume;
  if (order_state.data.volume_left > 0) {
    order_state.data.status = OrderStatus::PartialFilledActive;
  }
  writer->write(now(), order_state.data);
}

void TraderXTP::OnCancelOrderError(XTPOrderCancelInfo *cancel_info, XTPRI *error_info, uint64_t session_id) {
  SPDLOG_ERROR("cancel order error, cancel_info: {}, error_id: {}, error_msg: {}, session_id: {}",
               to_string(*cancel_info), error_info->error_id, error_info->error_msg, session_id);
}

void TraderXTP::OnQueryPosition(XTPQueryStkPositionRsp *position, XTPRI *error_info, int request_id, bool is_last,
                                uint64_t session_id) {
  if (error_info != nullptr && error_info->error_id != 0) {
    SPDLOG_ERROR("error_id:{}, error_msg: {}, request_id: {}, last: {}", error_info->error_id, error_info->error_msg,
                 request_id, is_last);
    return;
  }
  SPDLOG_TRACE("OnQueryPosition: {}", to_string(*position));
  auto writer = get_position_writer();
  Position &stock_pos = writer->open_data<Position>(0);
  if (error_info == nullptr || error_info->error_id == 0) {
    from_xtp(*position, stock_pos);
  }
  stock_pos.holder_uid = get_home()->uid;
  stock_pos.instrument_type = get_instrument_type(stock_pos.exchange_id, stock_pos.instrument_id);
  stock_pos.direction = Direction::Long;
  strncpy(stock_pos.trading_day, this->trading_day_.c_str(), DATE_LEN);
  stock_pos.update_time = yijinjing::time::now_in_nano();
  writer->close_data();
  if (is_last) {
    PositionEnd &end = writer->open_data<PositionEnd>(0);
    end.holder_uid = get_home()->uid;
    writer->close_data();
    enable_positions_sync();
  }
}

void TraderXTP::OnQueryAsset(XTPQueryAssetRsp *asset, XTPRI *error_info, int request_id, bool is_last,
                             uint64_t session_id) {
  if (error_info != nullptr && error_info->error_id != 0) {
    SPDLOG_ERROR("error_id: {}, error_msg: {}, request_id: {}, last: {}", error_info->error_id, error_info->error_msg,
                 request_id, is_last);
  }
  if (error_info == nullptr || error_info->error_id == 0 || error_info->error_id == 11000350) {
    SPDLOG_TRACE("OnQueryAsset: {}", to_string(*asset));
    auto writer = get_asset_writer();
    Asset &account = writer->open_data<Asset>(0);
    if (error_info == nullptr || error_info->error_id == 0) {
      from_xtp(*asset, account);
    }
    strncpy(account.trading_day, this->trading_day_.c_str(), DATE_LEN);
    account.holder_uid = get_home()->uid;
    account.update_time = yijinjing::time::now_in_nano();
    writer->close_data();
    enable_asset_sync();
  }
}

bool TraderXTP::req_history_order(const event_ptr &event) {
  XTPQueryOrderReq query_param{};
  int request_id = request_id_++;
  int ret = api_->QueryOrders(&query_param, session_id_, request_id);
  if (0 != ret) {
    SPDLOG_ERROR("QueryOrders False: {}", ret);
  }
  map_request_location_.emplace(request_id, event->source());
  return 0 == ret;
}

bool TraderXTP::req_history_trade(const event_ptr &event) {
  XTPQueryTraderReq query_param{};
  int request_id = request_id_++;
  int ret = api_->QueryTrades(&query_param, session_id_, request_id);
  if (0 != ret) {
    SPDLOG_ERROR("QueryTrades False ： {}", ret);
  }
  map_request_location_.emplace(request_id, event->source());
  return 0 == ret;
}

void TraderXTP::OnQueryOrder(XTPQueryOrderRsp *order_info, XTPRI *error_info, int request_id, bool is_last,
                             uint64_t session_id) {
  auto writer = get_history_writer(request_id);
  HistoryOrder &history_order = writer->open_data<HistoryOrder>();

  if (order_info == nullptr) {
    SPDLOG_WARN("XTPQueryOrderRsp* order_info == nullptr, no data returned!");
    history_order.is_last = true;
    strncpy(history_order.error_msg, "返回数据为空, 可能代表无历史Order数据", ERROR_MSG_LEN);
    writer->close_data();
    return;
  }

  auto is_error = error_info != nullptr and error_info->error_id != 0;
  if (is_error) {
    SPDLOG_ERROR("OnQueryOrder False , error_code : {}, error_msg : {}", error_info->error_id, error_info->error_msg);
    history_order.error_id = error_info->error_id;
    strncpy(history_order.error_msg, error_info->error_msg, ERROR_MSG_LEN);
  }

  strncpy(history_order.trading_day, trading_day_.c_str(), DATE_LEN);
  from_xtp(*order_info, history_order);
  history_order.order_id = writer->current_frame_uid();
  history_order.is_last = is_last;
  history_order.insert_time = yijinjing::time::now_in_nano();
  history_order.update_time = history_order.insert_time;
  writer->close_data();
}

yijinjing::journal::writer_ptr TraderXTP::get_history_writer(uint64_t request_id) {
  return get_writer(map_request_location_.try_emplace(request_id).first->second);
}

void TraderXTP::OnQueryTrade(XTPQueryTradeRsp *trade_info, XTPRI *error_info, int request_id, bool is_last,
                             uint64_t session_id) {
  auto writer = get_history_writer(request_id);
  HistoryTrade &history_trade = writer->open_data<HistoryTrade>(now());

  if (trade_info == nullptr) {
    SPDLOG_WARN("XTPQueryTradeRsp* trade_info == nullptr, no data returned!");
    history_trade.is_last = true;
    strncpy(history_trade.error_msg, "返回数据为空, 可能代表无历史Trade数据", ERROR_MSG_LEN);
    writer->close_data();
    return;
  }

  auto is_error = error_info != nullptr and error_info->error_id != 0;
  if (is_error) {
    SPDLOG_ERROR("OnQueryTrade False , error_code : {}, error_msg : {}", error_info->error_id, error_info->error_msg);
    history_trade.error_id = error_info->error_id;
    strncpy(history_trade.error_msg, error_info->error_msg, ERROR_MSG_LEN);
  }

  from_xtp(*trade_info, history_trade);
  history_trade.trade_id = writer->current_frame_uid();
  history_trade.is_last = is_last;
  history_trade.trade_time = yijinjing::time::now_in_nano();
  strncpy(history_trade.trading_day, trading_day_.c_str(), DATE_LEN);
  history_trade.instrument_type = get_instrument_type(history_trade.exchange_id, history_trade.instrument_id);
  writer->close_data();
}

} // namespace kungfu::wingchun::xtp
