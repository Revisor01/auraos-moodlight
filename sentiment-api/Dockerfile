FROM python:3.12-slim

RUN apt-get update && apt-get install -y --no-install-recommends build-essential && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY requirements.txt .

RUN pip install --no-cache-dir -r requirements.txt

COPY . .

ENV PYTHONUNBUFFERED=1

EXPOSE 6237

# Gunicorn mit einem Worker (-w 1) damit Background Worker genau einmal laeuft
CMD ["gunicorn", "-w", "1", "--threads", "4", "-b", "0.0.0.0:6237", "--timeout", "120", "--access-logfile", "-", "app:app"]
