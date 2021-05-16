import { Subject } from 'rxjs';
import { map } from 'rxjs/operators';

import { 
    watcher,

    dealGatewayStates, 
    transformTradingItemListToData, 
    transformOrderTradeListToData,
    transformOrderInputListToData, 
    transformOrderStatListToData, 
    transformAssetItemListToData,

    dealOrderInput,
    dealOrder,
    dealTrade,
    dealPos,
    dealAsset,
    dealSnapshot,
} from '__io/kungfu/watcher';

import { setTimerPromiseTask, ensureLeaderData, resolveInstruments } from '__gUtils/busiUtils';


const deamonDataSubject: any = new Subject();

const appDataSubject: any = new Subject();



(() => {

    if (watcher.noWathcer) {
        return;
    };  
    
    if (process.env.APP_TYPE !== 'daemon') {
        if (process.env.APP_TYPE !== 'cli') {
            return;
        };
    };

    setTimerPromiseTask(async () => {

            const ledgerData = watcher.ledger;

            console.time('deal')
            //限制最大内存/cpu使用
            const orderInputOrigins = ensureLeaderData(ledgerData.OrderInput, 'insert_time').slice(0, 1500);
            const orderOrigins = ensureLeaderData(ledgerData.Order, 'update_time').slice(0, 1500);
            const tradeOrigins = ensureLeaderData(ledgerData.Trade, 'trade_time').slice(0, 1500);
            console.log('len', orderOrigins.length, tradeOrigins.length)

            const accountStrategyOrderInputs = await transformOrderInputListToData(orderInputOrigins, dealOrderInput)
            const accountStrategyOrders = await transformOrderTradeListToData(orderOrigins, dealOrder);
            const accountStrategyTrades = await transformOrderTradeListToData(tradeOrigins, dealTrade);

            const allOrders = orderOrigins.slice(0, 100).map((item: OrderOriginData) => dealOrder(item));
            const allTrades = tradeOrigins.slice(0, 100).map((item: TradeOriginData) => dealTrade(item));
            console.timeEnd('deal')


            const positions = ensureLeaderData(ledgerData.Position).map((item: PosOriginData) => dealPos(item));
            const positionsByTicker = transformTradingItemListToData(positions, 'ticker');
            const assets = ensureLeaderData(ledgerData.Asset).map((item: AssetOriginData) => dealAsset(item));
            const pnl = ensureLeaderData(ledgerData.AssetSnapshot, 'update_time').map((item: AssetSnapshotOriginData) => dealSnapshot(item));
            const dailyAsset = ensureLeaderData(ledgerData.DailyAsset, 'trading_day').map((item: AssetSnapshotOriginData) => dealSnapshot(item));
            const instruments = ensureLeaderData(ledgerData.Instrument);
            const instrumentsAfterFilter = instruments
                .filter((item: InstrumentOriginData) => {
                    //普通股票 期货 股票期权 基金 科创板股票 指数
                    if (item.instrument_type === 1) return true;
                    if (item.instrument_type === 2) return true;
                    if (item.instrument_type === 4) return true;
                    if (item.instrument_type === 5) return true;
                    if (item.instrument_type === 6) return true;
                    if (item.instrument_type === 7) return true;
    
                    return false
                });
            const instrumentsPipeData = resolveInstruments(instrumentsAfterFilter);
            const quotes = ensureLeaderData(ledgerData.Quote);

            const accountTradingDataPipeData = {
                orders: accountStrategyOrders.account || {},
                trades: accountStrategyTrades.account || {},
                positions: transformTradingItemListToData(positions, 'account'),
                ordersByTicker: allOrders,
                tradesByTicker: allTrades,
                positionsByTicker,
                assets: transformAssetItemListToData(assets, 'account'),
                pnl: transformTradingItemListToData(pnl, 'account'),
                dailyPnl: transformTradingItemListToData(dailyAsset, 'account'),
            }
    
            const strategyTradingDataPipeData = {
                orders: accountStrategyOrders.strategy || {},
                orderInputs: accountStrategyOrderInputs.strategy || {},
                trades: accountStrategyTrades.strategy || {},
                positions: transformTradingItemListToData(positions, 'strategy'),
                assets: transformAssetItemListToData(assets, 'strategy'),
                pnl: transformTradingItemListToData(pnl, 'strategy'),
                dailyPnl: transformTradingItemListToData(dailyAsset, 'strategy'),
            }

            const allOrdersPipeData = {
                orders: allOrders,
            }
    
            deamonDataSubject.next({
                accountTradingDataPipeData,
                strategyTradingDataPipeData,
                instrumentsPipeData,
                allOrdersPipeData,
                quotes,
                globalPipeData: {
                    watcherIsLive: watcher.isLive() || false,
                    gatewayStates: dealGatewayStates(watcher.appStates)
                }
            })

            return true

    }, 1000)

})();

(() => {

    if (watcher.noWathcer) {
        return;
    }

    if (process.env.RENDERER_TYPE !== 'app') {
            return;
    }
    setTimerPromiseTask(() => {
        return new Promise(resolve => {
            const stateData = watcher.state;
            const ledgerData = watcher.ledger;
            const timeValueList = ensureLeaderData(stateData.TimeValue.filter('tag_c', 'task'), 'update_time').slice(0, 100)
            const orderStat = ensureLeaderData(ledgerData.OrderStat, 'update_time');
            const orderStatResolved = transformOrderStatListToData(orderStat);            
            appDataSubject.next({
                timeValueList: timeValueList,
                orderStat: orderStatResolved
            })

            resolve(true)
        })
       
    }, 1000)

})();


export const buildTradingDataPipe = (type: string) => {
    return deamonDataSubject.pipe(
        map((data: any) => {
            return type === 'account' ? data.accountTradingDataPipeData : data.strategyTradingDataPipeData;
        })
    )
}

export const buildInstrumentsPipe = () => {
    return deamonDataSubject.pipe(
        map((data: any) => {
            return {
                instruments: data.instrumentsPipeData
            }
        })
    )

}

export const buildAllOrdersPipe = () => {
    return deamonDataSubject.pipe(
        map((data: any) => {
            return data.allOrdersPipeData
        })
    )
}

export const buildMarketDataPipe = () => {
    return deamonDataSubject.pipe(
        map((data: any) => {
            return data.quotes
        })
    )
}

export const buildKungfuGlobalDataPipe = () => {
    return deamonDataSubject.pipe(
        map((data: any) => {
            return data.globalPipeData
        })
    )
}

export const buildTaskDataPipe = () => {
    return appDataSubject.pipe(
        map((data: any) => {
            return {
                timeValueList: data.timeValueList
            };
        })
    )
}

export const buildOrderStatDataPipe = () => {
    return appDataSubject.pipe(
        map((data: any) => {
            return {
                orderStat: data.orderStat
            };
        })
    )
}


