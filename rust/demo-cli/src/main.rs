use anyhow::Result;
use clap::Parser;
use futures_lite::StreamExt;
use lapin::{options::*, types::FieldTable, BasicProperties, Connection, ConnectionProperties};
use nervous_system_common::{AlarmDecision, AlarmType, DeviceEvent, Protocol, Severity};
use std::time::{SystemTime, UNIX_EPOCH};
use tokio::time::{sleep, Duration};
use tracing::{error, info, warn};

const RAW_QUEUE: &str = "device.events.raw";
const DISPATCH_QUEUE: &str = "alarms.dispatch";

#[derive(Parser, Debug)]
#[command(name = "Nervous System Demo")]
#[command(about = "Interactive demo CLI for the Nervous System alarm pipeline")]
struct Args {
    /// AMQP connection URI
    #[arg(long, default_value = "amqp://guest:guest@localhost:5672")]
    amqp_uri: String,

    /// Interval between event publishes (ms)
    #[arg(long, default_value = "2000")]
    interval_ms: u64,

    /// Specific event type to publish (motion_detected, door_opened, smoke_detected, flood_detected, temperature_reading)
    #[arg(long)]
    event: Option<String>,

    /// Number of events to publish before stopping (default: loop forever)
    #[arg(long)]
    count: Option<usize>,

    /// Print raw JSON payloads
    #[arg(long)]
    verbose: bool,
}

fn now_ms() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_millis() as u64
}

fn get_events() -> Vec<DeviceEvent> {
    vec![
        DeviceEvent {
            device_id: "sim-mqtt-001".into(),
            protocol: Protocol::Mqtt,
            event_type: "motion_detected".into(),
            timestamp_ms: now_ms(),
            payload: serde_json::json!({"zone": "front-door"}),
        },
        DeviceEvent {
            device_id: "sim-zigbee-001".into(),
            protocol: Protocol::Zigbee,
            event_type: "door_opened".into(),
            timestamp_ms: now_ms(),
            payload: serde_json::json!({"door": "main-entrance"}),
        },
        DeviceEvent {
            device_id: "sim-http-001".into(),
            protocol: Protocol::Http,
            event_type: "temperature_reading".into(),
            timestamp_ms: now_ms(),
            payload: serde_json::json!({"celsius": 22.5}),
        },
        DeviceEvent {
            device_id: "sim-mqtt-002".into(),
            protocol: Protocol::Mqtt,
            event_type: "smoke_detected".into(),
            timestamp_ms: now_ms(),
            payload: serde_json::json!({"zone": "kitchen"}),
        },
        DeviceEvent {
            device_id: "sim-zigbee-002".into(),
            protocol: Protocol::Zigbee,
            event_type: "flood_detected".into(),
            timestamp_ms: now_ms(),
            payload: serde_json::json!({"zone": "basement", "water_level_mm": 12}),
        },
    ]
}

fn get_event_by_type(event_type: &str) -> Option<DeviceEvent> {
    get_events()
        .into_iter()
        .find(|e| e.event_type == event_type)
}

#[tokio::main]
async fn main() -> Result<()> {
    dotenvy::dotenv().ok();
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "demo=info".into()),
        )
        .with_target(false)
        .init();

    let args = Args::parse();

    info!("═══════════════════════════════════════════════════════════");
    info!("  Nervous System Demo CLI");
    info!("═══════════════════════════════════════════════════════════");
    info!("  AMQP URI: {}", args.amqp_uri);
    info!("  Interval: {}ms", args.interval_ms);
    if let Some(ref ev) = args.event {
        info!("  Event type: {}", ev);
    }
    if let Some(c) = args.count {
        info!("  Publishing {} events", c);
    } else {
        info!("  Publishing continuously (Ctrl-C to stop)");
    }
    info!("═══════════════════════════════════════════════════════════");

    let conn = Connection::connect(&args.amqp_uri, ConnectionProperties::default()).await?;
    let pub_channel = conn.create_channel().await?;
    let sub_channel = conn.create_channel().await?;

    pub_channel
        .queue_declare(
            RAW_QUEUE,
            QueueDeclareOptions {
                durable: true,
                ..Default::default()
            },
            FieldTable::default(),
        )
        .await?;

    sub_channel
        .queue_declare(
            DISPATCH_QUEUE,
            QueueDeclareOptions {
                durable: true,
                ..Default::default()
            },
            FieldTable::default(),
        )
        .await?;

    let publisher = tokio::spawn(publish_loop(
        pub_channel,
        args.event,
        args.count,
        args.interval_ms,
        args.verbose,
    ));

    let subscriber = tokio::spawn(subscribe_loop(sub_channel, args.verbose));

    tokio::select! {
        _ = publisher => {},
        _ = subscriber => {},
    }

    Ok(())
}

async fn publish_loop(
    channel: lapin::Channel,
    event_filter: Option<String>,
    count: Option<usize>,
    interval_ms: u64,
    verbose: bool,
) -> Result<()> {
    let events = get_events();
    let mut idx = 0;
    let mut published = 0;

    loop {
        let event = if let Some(ref filter) = event_filter {
            match get_event_by_type(filter) {
                Some(mut ev) => {
                    ev.timestamp_ms = now_ms();
                    ev
                }
                None => {
                    warn!("unknown event type: {}", filter);
                    sleep(Duration::from_millis(interval_ms)).await;
                    continue;
                }
            }
        } else {
            let mut ev = events[idx % events.len()].clone();
            ev.timestamp_ms = now_ms();
            idx += 1;
            ev
        };

        let body = serde_json::to_vec(&event)?;
        if verbose {
            println!("\n[EVENT] publishing: {}", serde_json::to_string(&event)?);
        } else {
            println!("\n[EVENT] {} | {} | {:?}", event.device_id, event.event_type, event.protocol);
        }

        channel
            .basic_publish(
                "",
                RAW_QUEUE,
                BasicPublishOptions::default(),
                &body,
                BasicProperties::default().with_delivery_mode(2),
            )
            .await?
            .await?;

        published += 1;
        if let Some(c) = count {
            if published >= c {
                info!("published {} events, exiting", c);
                break;
            }
        }

        sleep(Duration::from_millis(interval_ms)).await;
    }

    Ok(())
}

async fn subscribe_loop(
    channel: lapin::Channel,
    verbose: bool,
) -> Result<()> {
    let mut consumer = channel
        .basic_consume(
            DISPATCH_QUEUE,
            "",
            BasicConsumeOptions {
                no_ack: false,
                ..Default::default()
            },
            FieldTable::default(),
        )
        .await?;

    while let Some(delivery) = consumer.next().await {
        match delivery {
            Ok(delivery) => {
                if let Ok(alarm) = serde_json::from_slice::<AlarmDecision>(&delivery.data) {
                    if verbose {
                        println!("\n[ALARM] received: {}", serde_json::to_string(&alarm)?);
                    } else {
                        let severity_str = match alarm.severity {
                            Severity::Low => "LOW",
                            Severity::Medium => "MEDIUM",
                            Severity::High => "HIGH",
                            Severity::Critical => "🔴 CRITICAL 🔴",
                        };
                        let alarm_type_str = match &alarm.alarm_type {
                            AlarmType::Motion => "motion",
                            AlarmType::Intrusion => "intrusion",
                            AlarmType::Fire => "fire",
                            AlarmType::Flood => "flood",
                            AlarmType::Custom(s) => s,
                        };
                        println!(
                            "\n[ALARM] {} | {} | {} | {}",
                            alarm_type_str, severity_str, alarm.message, alarm.device_id
                        );
                    }
                    delivery.ack(BasicAckOptions::default()).await?;
                } else {
                    warn!("failed to parse alarm");
                    delivery.nack(BasicNackOptions::default()).await?;
                }
            }
            Err(e) => error!("consumer error: {}", e),
        }
    }

    Ok(())
}
