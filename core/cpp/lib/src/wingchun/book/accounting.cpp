//
// Created by Keren Dong on 2020/4/4.
//

#include <kungfu/wingchun/book/book.h>

using namespace kungfu::longfist::enums;
using namespace kungfu::longfist::types;
using namespace kungfu::wingchun;
using namespace kungfu::yijinjing;
using namespace kungfu::yijinjing::data;

namespace kungfu::wingchun::book {
class StockAccountingMethod : public AccountingMethod {
public:
  static constexpr double min_comission = 5;
  static constexpr double commission_ratio = 0.0008;

  StockAccountingMethod() = default;

  void apply_trading_day(Book_ptr book, int64_t trading_day) override {}

  void apply_quote(Book_ptr book, const Quote &quote) override {
    auto &position = book->get_long_position(quote);
    if (is_valid_price(quote.close_price)) {
      position.close_price = quote.close_price;
    }
    if (is_valid_price(quote.last_price)) {
      position.last_price = quote.last_price;
    }
    if (is_valid_price(quote.pre_close_price)) {
      position.pre_close_price = quote.pre_close_price;
    }
    update_unrealized_pnl(position);
  }

  void apply_order_input(Book_ptr book, const OrderInput &input) override {
    auto &position = book->get_position(input);
    if (input.side == Side::Sell and position.yesterday_volume - position.frozen_yesterday > input.volume) {
      position.frozen_total += input.volume;
      position.frozen_yesterday += input.volume;
    }
    if (input.side == Side::Buy) {
      book->asset.frozen_cash += input.volume * input.frozen_price;
      book->asset.avail -= input.volume * input.frozen_price;
    }
    update_unrealized_pnl(position);
  }

  void apply_order(Book_ptr book, const Order &order) override {
    auto &position = book->get_position(order);
    auto status_ok = order.status != OrderStatus::Submitted and order.status != OrderStatus::Pending and
                     order.status != OrderStatus ::PartialFilledActive;
    if (status_ok and order.volume_left > 0) {
      if (order.side == Side::Sell) {
        position.frozen_total -= order.volume_left;
        position.frozen_yesterday -= order.volume_left;
      }
      if (order.side == Side::Buy) {
        auto frozen_amount = order.volume_left * order.frozen_price;
        book->asset.frozen_cash -= frozen_amount;
        book->asset.avail += frozen_amount;
      }
    }
    update_unrealized_pnl(position);
  }

  void apply_trade(Book_ptr book, const Trade &trade) override {
    if (trade.side == Side::Sell) {
      apply_sell(book, trade);
    }
    if (trade.side == Side::Buy) {
      apply_buy(book, trade);
    }
    update_unrealized_pnl(book->get_position(trade));
  }

private:
  std::unordered_map<uint64_t, double> commission_map_ = {};

  void apply_buy(Book_ptr &book, const Trade &trade) {
    auto &position = book->get_position(trade);
    if (position.volume + trade.volume > 0) {
      position.avg_open_price = (position.avg_open_price * position.volume + trade.price * trade.volume) /
                                (double)(position.volume + trade.volume);
    }
    auto commission = calculate_commission(trade);
    auto tax = calculate_tax(trade);
    position.volume += trade.volume;

    update_unrealized_pnl(position);

    book->asset.frozen_cash -= book->get_frozen_price(trade.order_id) * trade.volume;
    book->asset.avail -= commission;
    book->asset.avail -= tax;
    book->asset.intraday_fee += commission + tax;
    book->asset.accumulated_fee += commission + tax;
  }

  void apply_sell(Book_ptr &book, const Trade &trade) {
    auto &position = book->get_position(trade);
    if (position.yesterday_volume < trade.volume) {
      return;
    }
    if (position.frozen_total < trade.volume) {
      return;
    }
    auto realized_pnl = (trade.price - position.avg_open_price) * trade.volume;
    auto commission = calculate_commission(trade);
    auto tax = calculate_tax(trade);
    position.frozen_total -= trade.volume;
    position.frozen_yesterday -= trade.volume;
    position.yesterday_volume -= trade.volume;
    position.volume -= trade.volume;
    position.realized_pnl += realized_pnl;

    update_unrealized_pnl(position);

    book->asset.realized_pnl += realized_pnl;
    book->asset.avail += trade.price * trade.volume;
    book->asset.avail -= commission;
    book->asset.avail -= tax;
    book->asset.intraday_fee += commission + tax;
    book->asset.accumulated_fee += commission + tax;
  }

  double calculate_commission(const Trade &trade) {
    if (commission_map_.find(trade.order_id) == commission_map_.end()) {
      commission_map_.emplace(trade.order_id, min_comission);
    }
    auto commission = commission_map_[trade.order_id];
    auto amount = trade.price * trade.volume * commission_ratio;
    if (commission == min_comission) {
      if (amount > commission) {
        commission_map_.emplace(trade.order_id, 0);
        return amount;
      } else {
        commission_map_[trade.order_id] = commission_map_[trade.order_id] - amount;
        return min_comission;
      }
    } else {
      if (amount > commission) {
        commission_map_.emplace(trade.order_id, 0);
        return amount - commission;
      } else {
        commission_map_[trade.order_id] = commission_map_[trade.order_id] - amount;
        return 0;
      }
    }
  }

  static double calculate_tax(const Trade &trade) {
    return trade.side == Side::Sell ? trade.price * trade.volume * 0.001 : 0;
  }

  static void update_unrealized_pnl(Position &position) {
    if (position.last_price > 0) {
      position.unrealized_pnl = (position.last_price - position.avg_open_price) * position.volume;
    }
  }
};

class FutureAccountingMethod : public AccountingMethod {
public:
  FutureAccountingMethod() = default;

  void apply_trading_day(Book_ptr book, int64_t trading_day) override {}

  void apply_quote(Book_ptr book, const Quote &quote) override {}

  void apply_order_input(Book_ptr book, const OrderInput &input) override {}

  void apply_order(Book_ptr book, const Order &order) override {}

  void apply_trade(Book_ptr book, const Trade &trade) override {}

private:
  std::unordered_map<uint64_t, double> commission_map_ = {};
};

void AccountingMethod::setup_defaults(Bookkeeper &bookkeeper) {
  auto stock_accounting_method = std::make_shared<StockAccountingMethod>();
  auto future_accounting_method = std::make_shared<FutureAccountingMethod>();

  bookkeeper.set_accounting_method(InstrumentType::Stock, stock_accounting_method);
  bookkeeper.set_accounting_method(InstrumentType::Bond, stock_accounting_method);
  bookkeeper.set_accounting_method(InstrumentType::Fund, stock_accounting_method);
  bookkeeper.set_accounting_method(InstrumentType::StockOption, stock_accounting_method);
  bookkeeper.set_accounting_method(InstrumentType::TechStock, stock_accounting_method);
  bookkeeper.set_accounting_method(InstrumentType::Index, stock_accounting_method);
  bookkeeper.set_accounting_method(InstrumentType::Repo, stock_accounting_method);

  bookkeeper.set_accounting_method(InstrumentType::Future, future_accounting_method);

  bookkeeper.set_accounting_method(InstrumentType::Unknown, stock_accounting_method);
}
} // namespace kungfu::wingchun::book