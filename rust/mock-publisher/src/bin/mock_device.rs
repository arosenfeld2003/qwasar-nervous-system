use anyhow::Result;
use lapin::{options::*, types::FieldTable, BasicProperties, Connection, ConnectionProperties};
use nervous_system_common::{DeviceEvent, Protocol};
use std::time::{SystemTime, UNIX_EPOCH};
use tokio::time::{sleep, Duration};
use tracing::info;

const QUEUE: &str = "device.events.raw";

fn now_ms() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_millis() as u64
}

// Each call returns a fresh batch with current timestamps.
fn event_templates() -> Vec<DeviceEvent> {
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

#[tokio::main]
async fn main() -> Result<()> {
    dotenvy::dotenv().ok();
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "mock_device=info".into()),
        )
        .init();

    let amqp_uri = std::env::var("AMQP_URI")
        .unwrap_or_else(|_| "amqp://guest:guest@localhost:5672".into());
    let interval_ms: u64 = std::env::var("PUBLISH_INTERVAL_MS")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(2000);

    info!("connecting to RabbitMQ at {}", amqp_uri);
    let conn = Connection::connect(&amqp_uri, ConnectionProperties::default()).await?;
    let channel = conn.create_channel().await?;

    channel
        .queue_declare(
            QUEUE,
            QueueDeclareOptions {
                durable: true,
                ..Default::default()
            },
            FieldTable::default(),
        )
        .await?;

    info!("publishing mock device events every {}ms — Ctrl-C to stop", interval_ms);

    let templates = event_templates();
    let count = templates.len();

    for (i, _) in std::iter::repeat(()).enumerate() {
        let mut event = event_templates().remove(i % count);
        event.timestamp_ms = now_ms();

        let body = serde_json::to_vec(&event)?;
        channel
            .basic_publish(
                "",
                QUEUE,
                BasicPublishOptions::default(),
                &body,
                BasicProperties::default().with_delivery_mode(2), // persistent
            )
            .await?
            .await?;

        info!(
            device_id = %event.device_id,
            event_type = %event.event_type,
            "published"
        );

        sleep(Duration::from_millis(interval_ms)).await;
    }

    Ok(())
}
