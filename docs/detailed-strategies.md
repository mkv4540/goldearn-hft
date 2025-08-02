# GoldEarn HFT - Detailed Trading Strategies
*Prepared by Claude (Acting Head of Trading + Lead Quant Researcher)*

## 🎯 STRATEGY OVERVIEW

### Our Multi-Strategy Approach
1. **Market Making (60% of capital)** - Primary revenue driver
2. **Statistical Arbitrage (25% of capital)** - Alpha generation
3. **Cross-Asset Arbitrage (10% of capital)** - Opportunistic
4. **Momentum/Mean Reversion (5% of capital)** - Directional

## 📊 STRATEGY 1: MARKET MAKING

### Mathematical Framework

#### **Fair Value Estimation**
```python
def calculate_fair_value(order_book, recent_trades, external_signals):
    """
    Multi-signal fair value estimation
    """
    # Weighted mid-price (40% weight)
    mid_price = (order_book.best_bid + order_book.best_ask) / 2
    
    # Volume-weighted average price (30% weight)
    vwap = sum(trade.price * trade.volume for trade in recent_trades) / \
           sum(trade.volume for trade in recent_trades)
    
    # Microstructure signal (20% weight)
    flow_imbalance = calculate_flow_imbalance(recent_trades)
    microstructure_adj = flow_imbalance * 0.1  # 10 bps max adjustment
    
    # External signals (10% weight)
    index_signal = calculate_index_beta_adjustment(external_signals)
    
    fair_value = (0.4 * mid_price + 
                  0.3 * vwap + 
                  0.2 * (mid_price + microstructure_adj) +
                  0.1 * index_signal)
    
    return fair_value

def calculate_flow_imbalance(trades, window_minutes=5):
    """
    Calculate buying vs selling pressure
    """
    recent_trades = [t for t in trades if t.timestamp > now() - timedelta(minutes=window_minutes)]
    
    buy_volume = sum(t.volume for t in recent_trades if t.side == 'BUY')
    sell_volume = sum(t.volume for t in recent_trades if t.side == 'SELL')
    
    if buy_volume + sell_volume == 0:
        return 0
    
    return (buy_volume - sell_volume) / (buy_volume + sell_volume)
```

#### **Dynamic Spread Calculation**
```python
class DynamicSpreadCalculator:
    def __init__(self):
        self.base_spread_bps = 5  # 5 basis points base
        self.volatility_multiplier = 2.0
        self.inventory_skew_factor = 0.3
        
    def calculate_spread(self, symbol_data, inventory, market_conditions):
        """
        Calculate optimal bid-ask spread
        """
        # Base spread
        base_spread = self.base_spread_bps / 10000
        
        # Volatility adjustment
        volatility = calculate_volatility(symbol_data.recent_prices)
        vol_adjustment = 1 + (volatility - 0.01) * self.volatility_multiplier
        
        # Inventory skew
        max_inventory = symbol_data.position_limit
        inventory_ratio = inventory / max_inventory
        inventory_skew = abs(inventory_ratio) * self.inventory_skew_factor
        
        # Market condition adjustment
        liquidity_factor = calculate_liquidity_factor(symbol_data.order_book)
        competition_factor = calculate_competition_factor(symbol_data.order_book)
        
        # Final spread calculation
        adjusted_spread = (base_spread * 
                          vol_adjustment * 
                          (1 + inventory_skew) * 
                          liquidity_factor * 
                          competition_factor)
        
        return max(adjusted_spread, symbol_data.min_spread)

def calculate_volatility(prices, window=100):
    """
    Calculate realized volatility
    """
    if len(prices) < 2:
        return 0.01  # Default 1%
    
    returns = [log(prices[i]/prices[i-1]) for i in range(1, len(prices))]
    return np.std(returns) * sqrt(252 * 390)  # Annualized vol
```

#### **Position Skewing Algorithm**
```python
def calculate_position_skew(current_position, max_position, skew_factor=0.5):
    """
    Skew quotes based on current inventory
    """
    if max_position == 0:
        return 0, 0
    
    position_ratio = current_position / max_position
    skew = position_ratio * skew_factor
    
    # If long inventory, widen ask and narrow bid
    bid_skew = -skew  # Negative skew narrows bid
    ask_skew = skew   # Positive skew widens ask
    
    return bid_skew, ask_skew

def generate_market_making_quotes(symbol, fair_value, spread, inventory):
    """
    Generate bid/ask quotes with inventory skewing
    """
    bid_skew, ask_skew = calculate_position_skew(
        inventory.current_position, 
        inventory.max_position
    )
    
    # Calculate raw bid/ask
    half_spread = spread / 2
    raw_bid = fair_value - half_spread
    raw_ask = fair_value + half_spread
    
    # Apply skewing
    final_bid = raw_bid * (1 + bid_skew)
    final_ask = raw_ask * (1 + ask_skew)
    
    # Ensure minimum tick size
    final_bid = round_to_tick_size(final_bid, symbol.tick_size)
    final_ask = round_to_tick_size(final_ask, symbol.tick_size)
    
    return final_bid, final_ask
```

### Market Making Implementation

#### **Core Market Making Engine**
```cpp
class MarketMakingEngine {
private:
    std::unordered_map<std::string, SymbolState> symbol_states_;
    std::unordered_map<std::string, Position> positions_;
    RiskLimits risk_limits_;
    
public:
    void on_market_data(const MarketDataEvent& event) {
        auto& state = symbol_states_[event.symbol];
        
        // Update order book
        state.order_book.update(event);
        
        // Calculate new fair value
        double fair_value = calculate_fair_value(state);
        
        // Calculate optimal spread
        double spread = calculate_optimal_spread(state);
        
        // Generate new quotes if significant change
        if (should_requote(state, fair_value, spread)) {
            auto quotes = generate_quotes(event.symbol, fair_value, spread);
            submit_quotes(quotes);
        }
    }
    
private:
    bool should_requote(const SymbolState& state, double fair_value, double spread) {
        // Requote conditions:
        // 1. Fair value moved > 0.5 * spread
        // 2. Spread changed > 20%
        // 3. No quotes in market
        // 4. Risk limits breached
        
        double fair_value_change = abs(fair_value - state.last_fair_value);
        double spread_change = abs(spread - state.last_spread) / state.last_spread;
        
        return (fair_value_change > 0.5 * spread) ||
               (spread_change > 0.2) ||
               (!state.has_active_quotes) ||
               (check_risk_limits(state.symbol));
    }
};
```

## 📈 STRATEGY 2: STATISTICAL ARBITRAGE

### Pairs Trading Framework

#### **Cointegration Analysis**
```python
import numpy as np
import pandas as pd
from statsmodels.tsa.stattools import coint
from statsmodels.regression.linear_model import OLS

class PairsTrading:
    def __init__(self):
        self.pairs = []
        self.hedge_ratios = {}
        self.entry_threshold = 2.0  # Z-score
        self.exit_threshold = 0.5
        
    def find_cointegrated_pairs(self, price_data, lookback_days=252):
        """
        Find cointegrated pairs using Engle-Granger test
        """
        symbols = price_data.columns
        pairs = []
        
        for i in range(len(symbols)):
            for j in range(i+1, len(symbols)):
                symbol1, symbol2 = symbols[i], symbols[j]
                
                # Get price series
                y = price_data[symbol1].dropna()
                x = price_data[symbol2].dropna()
                
                # Align series
                common_dates = y.index.intersection(x.index)
                y = y[common_dates]
                x = x[common_dates]
                
                if len(common_dates) < lookback_days:
                    continue
                
                # Cointegration test
                score, p_value, _ = coint(y, x)
                
                if p_value < 0.05:  # 5% significance level
                    # Calculate hedge ratio
                    hedge_ratio = OLS(y, x).fit().params[0]
                    
                    pairs.append({
                        'symbol1': symbol1,
                        'symbol2': symbol2, 
                        'hedge_ratio': hedge_ratio,
                        'p_value': p_value,
                        'score': score
                    })
        
        return sorted(pairs, key=lambda x: x['p_value'])
    
    def calculate_spread(self, symbol1_price, symbol2_price, hedge_ratio):
        """
        Calculate spread between two securities
        """
        return symbol1_price - hedge_ratio * symbol2_price
    
    def calculate_zscore(self, spread_series, window=20):
        """
        Calculate rolling z-score of spread
        """
        rolling_mean = spread_series.rolling(window=window).mean()
        rolling_std = spread_series.rolling(window=window).std()
        
        return (spread_series - rolling_mean) / rolling_std
    
    def generate_signals(self, symbol1, symbol2, prices):
        """
        Generate trading signals for pair
        """
        hedge_ratio = self.hedge_ratios[(symbol1, symbol2)]
        
        # Calculate spread
        spread = self.calculate_spread(
            prices[symbol1], 
            prices[symbol2], 
            hedge_ratio
        )
        
        # Calculate z-score
        zscore = self.calculate_zscore(spread)
        current_zscore = zscore.iloc[-1]
        
        # Generate signals
        if current_zscore > self.entry_threshold:
            # Spread too high - short symbol1, long symbol2
            return [
                {'symbol': symbol1, 'side': 'SELL', 'quantity': 100},
                {'symbol': symbol2, 'side': 'BUY', 'quantity': int(100 * hedge_ratio)}
            ]
        elif current_zscore < -self.entry_threshold:
            # Spread too low - long symbol1, short symbol2
            return [
                {'symbol': symbol1, 'side': 'BUY', 'quantity': 100},
                {'symbol': symbol2, 'side': 'SELL', 'quantity': int(100 * hedge_ratio)}
            ]
        elif abs(current_zscore) < self.exit_threshold:
            # Close positions
            return self.generate_exit_signals(symbol1, symbol2)
        
        return []
```

#### **Mean Reversion Strategy**
```python
class MeanReversionStrategy:
    def __init__(self):
        self.lookback_period = 20
        self.entry_std = 2.0
        self.exit_std = 0.5
        self.max_holding_period = 5  # days
        
    def calculate_bollinger_bands(self, prices, window=20, num_std=2):
        """
        Calculate Bollinger Bands
        """
        rolling_mean = prices.rolling(window=window).mean()
        rolling_std = prices.rolling(window=window).std()
        
        upper_band = rolling_mean + (rolling_std * num_std)
        lower_band = rolling_mean - (rolling_std * num_std)
        
        return upper_band, rolling_mean, lower_band
    
    def generate_mean_reversion_signal(self, symbol, price_history):
        """
        Generate mean reversion signals
        """
        if len(price_history) < self.lookback_period:
            return None
        
        current_price = price_history.iloc[-1]
        upper, middle, lower = self.calculate_bollinger_bands(price_history)
        
        current_upper = upper.iloc[-1]
        current_middle = middle.iloc[-1] 
        current_lower = lower.iloc[-1]
        
        # Entry signals
        if current_price > current_upper:
            # Price above upper band - expect reversion down
            return {
                'symbol': symbol,
                'side': 'SELL',
                'entry_price': current_price,
                'target_price': current_middle,
                'stop_loss': current_price * 1.01  # 1% stop loss
            }
        elif current_price < current_lower:
            # Price below lower band - expect reversion up
            return {
                'symbol': symbol,
                'side': 'BUY', 
                'entry_price': current_price,
                'target_price': current_middle,
                'stop_loss': current_price * 0.99  # 1% stop loss
            }
        
        return None
```

## ⚡ STRATEGY 3: CROSS-ASSET ARBITRAGE

### Index Arbitrage (Nifty Futures vs Spot)

```python
class IndexArbitrageStrategy:
    def __init__(self):
        self.risk_free_rate = 0.06  # 6% annual
        self.transaction_cost = 0.0005  # 5 basis points
        
    def calculate_theoretical_futures_price(self, spot_price, dividend_yield, 
                                          time_to_expiry, risk_free_rate):
        """
        Calculate fair value of futures contract
        """
        return spot_price * exp((risk_free_rate - dividend_yield) * time_to_expiry)
    
    def identify_arbitrage_opportunity(self, nifty_spot, nifty_futures, 
                                     time_to_expiry, dividend_yield):
        """
        Identify index arbitrage opportunities
        """
        fair_value = self.calculate_theoretical_futures_price(
            nifty_spot, dividend_yield, time_to_expiry, self.risk_free_rate
        )
        
        # Calculate bounds including transaction costs
        upper_bound = fair_value * (1 + self.transaction_cost)
        lower_bound = fair_value * (1 - self.transaction_cost)
        
        if nifty_futures > upper_bound:
            # Futures overpriced - sell futures, buy underlying
            return {
                'action': 'SELL_FUTURES_BUY_SPOT',
                'futures_price': nifty_futures,
                'fair_value': fair_value,
                'profit_potential': nifty_futures - fair_value,
                'positions': [
                    {'instrument': 'NIFTY_FUT', 'side': 'SELL', 'quantity': 1},
                    {'instrument': 'NIFTY_BASKET', 'side': 'BUY', 'quantity': 1}
                ]
            }
        elif nifty_futures < lower_bound:
            # Futures underpriced - buy futures, sell underlying
            return {
                'action': 'BUY_FUTURES_SELL_SPOT',
                'futures_price': nifty_futures,
                'fair_value': fair_value,
                'profit_potential': fair_value - nifty_futures,
                'positions': [
                    {'instrument': 'NIFTY_FUT', 'side': 'BUY', 'quantity': 1},
                    {'instrument': 'NIFTY_BASKET', 'side': 'SELL', 'quantity': 1}
                ]
            }
        
        return None
```

### ETF Arbitrage

```python
class ETFArbitrageStrategy:
    def __init__(self):
        self.creation_unit_size = 50000  # Typical ETF creation unit
        self.transaction_costs = 0.001   # 10 basis points
        
    def calculate_nav(self, etf_holdings, underlying_prices):
        """
        Calculate Net Asset Value of ETF
        """
        total_value = 0
        total_shares = sum(holding['shares'] for holding in etf_holdings)
        
        for holding in etf_holdings:
            symbol = holding['symbol']
            shares = holding['shares']
            price = underlying_prices.get(symbol, 0)
            total_value += shares * price
        
        return total_value / total_shares if total_shares > 0 else 0
    
    def identify_etf_arbitrage(self, etf_price, nav, etf_symbol):
        """
        Identify ETF arbitrage opportunities
        """
        price_deviation = (etf_price - nav) / nav
        
        if abs(price_deviation) > self.transaction_costs:
            if price_deviation > 0:
                # ETF trading at premium - sell ETF, buy underlying
                return {
                    'action': 'SELL_ETF_BUY_BASKET',
                    'etf_price': etf_price,
                    'nav': nav,
                    'deviation': price_deviation,
                    'profit_potential': etf_price - nav
                }
            else:
                # ETF trading at discount - buy ETF, sell underlying
                return {
                    'action': 'BUY_ETF_SELL_BASKET', 
                    'etf_price': etf_price,
                    'nav': nav,
                    'deviation': price_deviation,
                    'profit_potential': nav - etf_price
                }
        
        return None
```

## 🚀 STRATEGY 4: MOMENTUM/TREND FOLLOWING

### Multi-Timeframe Momentum

```python
class MomentumStrategy:
    def __init__(self):
        self.short_period = 10
        self.long_period = 30
        self.rsi_period = 14
        self.rsi_overbought = 70
        self.rsi_oversold = 30
        
    def calculate_rsi(self, prices, period=14):
        """
        Calculate Relative Strength Index
        """
        delta = prices.diff()
        gain = (delta.where(delta > 0, 0)).rolling(window=period).mean()
        loss = (-delta.where(delta < 0, 0)).rolling(window=period).mean()
        
        rs = gain / loss
        rsi = 100 - (100 / (1 + rs))
        
        return rsi
    
    def calculate_macd(self, prices, fast=12, slow=26, signal=9):
        """
        Calculate MACD indicator
        """
        ema_fast = prices.ewm(span=fast).mean()
        ema_slow = prices.ewm(span=slow).mean()
        
        macd = ema_fast - ema_slow
        signal_line = macd.ewm(span=signal).mean()
        histogram = macd - signal_line
        
        return macd, signal_line, histogram
    
    def generate_momentum_signal(self, symbol, price_history):
        """
        Generate momentum-based trading signals
        """
        if len(price_history) < max(self.long_period, self.rsi_period, 26):
            return None
        
        # Calculate indicators
        rsi = self.calculate_rsi(price_history)
        macd, signal_line, histogram = self.calculate_macd(price_history)
        
        # Moving averages
        sma_short = price_history.rolling(window=self.short_period).mean()
        sma_long = price_history.rolling(window=self.long_period).mean()
        
        # Current values
        current_price = price_history.iloc[-1]
        current_rsi = rsi.iloc[-1]
        current_macd = macd.iloc[-1]
        current_signal = signal_line.iloc[-1]
        current_sma_short = sma_short.iloc[-1]
        current_sma_long = sma_long.iloc[-1]
        
        # Signal generation
        bullish_signals = 0
        bearish_signals = 0
        
        # RSI signals
        if current_rsi < self.rsi_oversold:
            bullish_signals += 1
        elif current_rsi > self.rsi_overbought:
            bearish_signals += 1
        
        # MACD signals
        if current_macd > current_signal:
            bullish_signals += 1
        else:
            bearish_signals += 1
        
        # Moving average signals
        if current_sma_short > current_sma_long:
            bullish_signals += 1
        else:
            bearish_signals += 1
        
        # Generate final signal
        if bullish_signals >= 2:
            return {
                'symbol': symbol,
                'side': 'BUY',
                'confidence': bullish_signals / 3,
                'entry_price': current_price,
                'target_price': current_price * 1.02,  # 2% target
                'stop_loss': current_price * 0.98      # 2% stop loss
            }
        elif bearish_signals >= 2:
            return {
                'symbol': symbol,
                'side': 'SELL',
                'confidence': bearish_signals / 3,
                'entry_price': current_price,
                'target_price': current_price * 0.98,  # 2% target
                'stop_loss': current_price * 1.02     # 2% stop loss
            }
        
        return None
```

## 📊 STRATEGY PERFORMANCE TARGETS

### Expected Returns by Strategy

**Market Making:**
- Target Sharpe Ratio: 3-5
- Monthly Returns: 2-4%
- Maximum Drawdown: <2%
- Win Rate: 85-95%

**Statistical Arbitrage:**
- Target Sharpe Ratio: 2-3
- Monthly Returns: 3-6%
- Maximum Drawdown: <5%
- Win Rate: 60-70%

**Cross-Asset Arbitrage:**
- Target Sharpe Ratio: 4-6
- Monthly Returns: 1-3%
- Maximum Drawdown: <1%
- Win Rate: 90-95%

**Momentum Strategy:**
- Target Sharpe Ratio: 1.5-2.5
- Monthly Returns: 2-8%
- Maximum Drawdown: <8%
- Win Rate: 45-55%

### Risk Management per Strategy

```python
STRATEGY_RISK_LIMITS = {
    'market_making': {
        'max_position_per_symbol': 50000,      # ₹50k per stock
        'max_inventory_ratio': 0.3,            # 30% of daily volume
        'daily_loss_limit': 25000,             # ₹25k daily loss
        'max_correlation_exposure': 0.5        # 50% in correlated positions
    },
    'statistical_arbitrage': {
        'max_position_per_pair': 100000,       # ₹1 lakh per pair
        'max_holding_period': 5,               # 5 days maximum
        'daily_loss_limit': 50000,             # ₹50k daily loss
        'min_correlation': 0.7                 # Minimum correlation for pairs
    },
    'cross_asset_arbitrage': {
        'max_position_size': 200000,           # ₹2 lakh per opportunity
        'max_holding_period': 1,               # 1 day maximum
        'daily_loss_limit': 30000,             # ₹30k daily loss
        'min_arbitrage_profit': 0.002          # 20 bps minimum profit
    },
    'momentum': {
        'max_position_per_symbol': 75000,      # ₹75k per stock
        'max_holding_period': 10,              # 10 days maximum
        'daily_loss_limit': 40000,             # ₹40k daily loss
        'stop_loss_percentage': 0.02           # 2% stop loss
    }
}
```

This comprehensive strategy framework will generate consistent alpha while managing risk across multiple market conditions and timeframes.