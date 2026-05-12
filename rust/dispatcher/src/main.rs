use anyhow::Result;
use futures_lite::StreamExt;
use lapin::{options::*, types::FieldTable, Connection, ConnectionProperties};
use lettre::{
    message::header::ContentType, AsyncSmtpTransport, AsyncTransport, Message,
    Tokio1Executor,
};
use nervous_system_common::AlarmDecision;
use tracing::{error, info, warn};

const ALARM_QUEUE: &str = "alarms.dispatch";

// All dispatcher configuration lives here, loaded once at startup.
struct Config {
    amqp_uri: String,
    webhook_url: Option<String>,
    email: Option<EmailConfig>,
}

struct EmailConfig {
    smtp_host: String,
    smtp_port: u16,
    smtp_user: String,
    smtp_pass: String,
    from: String,
    to: String,
}

impl Config {
    fn from_env() -> Self {
        let email = match (
            std::env::var("SMTP_HOST").ok(),
            std::env::var("ALARM_EMAIL_FROM").ok(),
            std::env::var("ALARM_EMAIL_TO").ok(),
        ) {
            (Some(host), Some(from), Some(to)) => Some(EmailConfig {
                smtp_host: host,
                smtp_port: std::env::var("SMTP_PORT")
                    .ok()
                    .and_then(|p| p.parse().ok())
                    .unwrap_or(587),
                smtp_user: std::env::var("SMTP_USER").unwrap_or_default(),
                smtp_pass: std::env::var("SMTP_PASS").unwrap_or_default(),
                from,
                to,
            }),
            _ => None,
        };

        Self {
            amqp_uri: std::env::var("AMQP_URI")
                .unwrap_or_else(|_| "amqp://guest:guest@localhost:5672".into()),
            webhook_url: std::env::var("WEBHOOK_URL").ok(),
            email,
        }
    }
}

#[tokio::main]
async fn main() -> Result<()> {
    dotenvy::dotenv().ok();
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "nervous_system_dispatcher=info".into()),
        )
        .init();

    let config = Config::from_env();

    if config.webhook_url.is_none() && config.email.is_none() {
        warn!("no notification channels configured — set WEBHOOK_URL or SMTP_HOST+ALARM_EMAIL_FROM+ALARM_EMAIL_TO");
    }

    let http = reqwest::Client::new();

    info!("connecting to RabbitMQ at {}", config.amqp_uri);
    let conn = Connection::connect(&config.amqp_uri, ConnectionProperties::default()).await?;
    let channel = conn.create_channel().await?;

    channel
        .queue_declare(
            ALARM_QUEUE,
            QueueDeclareOptions {
                durable: true,
                ..Default::default()
            },
            FieldTable::default(),
        )
        .await?;

    info!("consuming alarms from '{}'", ALARM_QUEUE);

    let mut consumer = channel
        .basic_consume(
            ALARM_QUEUE,
            "dispatcher-service",
            BasicConsumeOptions::default(),
            FieldTable::default(),
        )
        .await?;

    while let Some(delivery) = consumer.next().await {
        match delivery {
            Ok(delivery) => {
                match serde_json::from_slice::<AlarmDecision>(&delivery.data) {
                    Ok(alarm) => {
                        info!(
                            device_id = %alarm.device_id,
                            severity = ?alarm.severity,
                            "dispatching alarm"
                        );
                        dispatch_alarm(&alarm, &config, &http).await;
                        // Always ack — a failed notification is logged but must not block the queue.
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

async fn dispatch_alarm(alarm: &AlarmDecision, config: &Config, http: &reqwest::Client) {
    if let Some(url) = &config.webhook_url {
        match http.post(url).json(alarm).send().await {
            Ok(resp) => info!(status = %resp.status(), "webhook delivered"),
            Err(e) => error!("webhook delivery failed: {}", e),
        }
    }

    if let Some(email_cfg) = &config.email {
        send_email(alarm, email_cfg).await;
    }
}

async fn send_email(alarm: &AlarmDecision, cfg: &EmailConfig) {
    let subject = format!(
        "[ALARM] {:?} — {:?} on {}",
        alarm.severity, alarm.alarm_type, alarm.device_id
    );
    let body = format!(
        "ALARM NOTIFICATION\n\
         ==================\n\
         Device:   {}\n\
         Type:     {:?}\n\
         Severity: {:?}\n\
         Message:  {}\n\
         Time:     {} ms (Unix epoch)\n",
        alarm.device_id,
        alarm.alarm_type,
        alarm.severity,
        alarm.message,
        alarm.triggered_at_ms,
    );

    let email = match Message::builder()
        .from(cfg.from.parse().unwrap())
        .to(cfg.to.parse().unwrap())
        .subject(subject)
        .header(ContentType::TEXT_PLAIN)
        .body(body)
    {
        Ok(m) => m,
        Err(e) => {
            error!("failed to build email message: {}", e);
            return;
        }
    };

    let transport = match build_smtp_transport(cfg) {
        Ok(t) => t,
        Err(e) => {
            error!("failed to build SMTP transport: {}", e);
            return;
        }
    };

    match transport.send(email).await {
        Ok(_) => info!(to = %cfg.to, "email delivered"),
        Err(e) => error!("email delivery failed: {}", e),
    }
}

fn build_smtp_transport(
    cfg: &EmailConfig,
) -> Result<AsyncSmtpTransport<Tokio1Executor>, lettre::transport::smtp::Error> {
    let transport = AsyncSmtpTransport::<Tokio1Executor>::starttls_relay(&cfg.smtp_host)?
        .port(cfg.smtp_port)
        .credentials(lettre::transport::smtp::authentication::Credentials::new(
            cfg.smtp_user.clone(),
            cfg.smtp_pass.clone(),
        ))
        .build();
    Ok(transport)
}
