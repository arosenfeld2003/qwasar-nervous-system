use anyhow::Result;
use futures_lite::StreamExt;
use lapin::{options::*, types::FieldTable, Connection, ConnectionProperties};
use mongodb::Client as MongoClient;
use nervous_system_common::DeviceEvent;
use tracing::{error, info, warn};

const INBOUND_QUEUE: &str = "device.events.raw";
const OUTBOUND_QUEUE: &str = "device.events.normalized";

struct Config {
    amqp_uri: String,
    mongo_uri: String,
}

impl Config {
    fn from_env() -> Self {
        Self {
            amqp_uri: std::env::var("AMQP_URI")
                .unwrap_or_else(|_| "amqp://guest:guest@localhost:5672".into()),
            mongo_uri: std::env::var("MONGO_URI")
                .unwrap_or_else(|_| "mongodb://localhost:27017".into()),
        }
    }
}

#[tokio::main]
async fn main() -> Result<()> {
    dotenvy::dotenv().ok();
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "nervous_system_queue=info".into()),
        )
        .init();

    let config = Config::from_env();

    info!("connecting to MongoDB at {}", config.mongo_uri);
    let mongo = MongoClient::with_uri_str(&config.mongo_uri).await?;
    let events_col = mongo
        .database("nervous_system")
        .collection::<serde_json::Value>("events");

    info!("connecting to RabbitMQ at {}", config.amqp_uri);
    let conn = Connection::connect(&config.amqp_uri, ConnectionProperties::default()).await?;
    let channel = conn.create_channel().await?;
    let publish_channel = conn.create_channel().await?;

    for queue in [INBOUND_QUEUE, OUTBOUND_QUEUE] {
        channel
            .queue_declare(
                queue,
                QueueDeclareOptions {
                    durable: true,
                    ..Default::default()
                },
                FieldTable::default(),
            )
            .await?;
    }

    info!("consuming from '{}'", INBOUND_QUEUE);

    let mut consumer = channel
        .basic_consume(
            INBOUND_QUEUE,
            "queue-service",
            BasicConsumeOptions::default(),
            FieldTable::default(),
        )
        .await?;

    while let Some(delivery) = consumer.next().await {
        match delivery {
            Ok(delivery) => {
                match serde_json::from_slice::<DeviceEvent>(&delivery.data) {
                    Ok(event) => {
                        info!(
                            device_id = %event.device_id,
                            event_type = %event.event_type,
                            "received event"
                        );

                        // Persist to MongoDB; a failure here is logged but does not drop the event.
                        let doc = serde_json::json!({
                            "device_id": event.device_id,
                            "protocol": event.protocol,
                            "event_type": event.event_type,
                            "timestamp_ms": event.timestamp_ms,
                            "payload": event.payload,
                            "received_at_ms": now_ms(),
                        });
                        if let Err(e) = events_col.insert_one(doc).await {
                            warn!("MongoDB insert failed (event not dropped): {}", e);
                        }

                        forward_event(&publish_channel, &event).await?;
                        delivery.ack(BasicAckOptions::default()).await?;
                    }
                    Err(e) => {
                        error!("deserialization failed, nacking: {}", e);
                        delivery.nack(BasicNackOptions::default()).await?;
                    }
                }
            }
            Err(e) => error!("consumer error: {}", e),
        }
    }

    Ok(())
}

async fn forward_event(channel: &lapin::Channel, event: &DeviceEvent) -> Result<()> {
    let payload = serde_json::to_vec(event)?;
    channel
        .basic_publish(
            "",
            OUTBOUND_QUEUE,
            BasicPublishOptions::default(),
            &payload,
            lapin::BasicProperties::default().with_delivery_mode(2),
        )
        .await?
        .await?;
    Ok(())
}

fn now_ms() -> u64 {
    use std::time::{SystemTime, UNIX_EPOCH};
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_millis() as u64
}
